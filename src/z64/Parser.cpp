#include "Parser.h"

#include "z64/Diagnostic.h"
#include "z64/Expression.h"
#include "z64/Initializer.h"
#include "z64/MacroTable.h"
#include "z64/Preprocessor.h"
#include "z64/Token.h"
#include "z64/Types.h"
#include "z64/Value.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace z64 {

namespace {

constexpr std::array Qualifiers = {
    std::string_view("static"),   std::string_view("const"),    std::string_view("extern"),
    std::string_view("volatile"), std::string_view("register"), std::string_view("inline"),
    std::string_view("unsigned"), std::string_view("signed"),   std::string_view("struct"),
    std::string_view("union"),    std::string_view("enum"),     std::string_view("_Alignas"),
};

constexpr std::array Skipped = {
    std::string_view("typedef"),
    std::string_view("struct"),
    std::string_view("union"),
    std::string_view("enum"),
};

[[nodiscard]] bool isQualifier(std::string_view text) {
    return std::ranges::find(Qualifiers, text) != Qualifiers.end();
}

[[nodiscard]] int nestingDelta(const Token& token) {
    if (token.kind != TokenKind::Punctuator || token.text.size() != 1) {
        return 0;
    }
    switch (token.text.front()) {
    case '(':
    case '[':
    case '{':
        return 1;
    case ')':
    case ']':
    case '}':
        return -1;
    default:
        return 0;
    }
}

[[nodiscard]] std::vector<Token> withoutAttributes(std::span<const Token> head) {
    std::vector<Token> kept;
    for (usize at = 0; at < head.size(); ++at) {
        if (isIdentifier(head[at], "__attribute__") && at + 1 < head.size() &&
            isPunctuator(head[at + 1], "(")) {
            int nesting = 0;
            for (++at; at < head.size(); ++at) {
                nesting += nestingDelta(head[at]);
                if (nesting == 0) {
                    break;
                }
            }
            continue;
        }
        kept.push_back(head[at]);
    }
    return kept;
}

[[nodiscard]] usize findNameIn(std::span<const Token> declarator) {
    for (usize cursor = declarator.size(); cursor > 0; --cursor) {
        const Token& token = declarator[cursor - 1];
        if (token.kind == TokenKind::Identifier && !isQualifier(token.text)) {
            return cursor - 1;
        }
    }
    return declarator.size();
}

void readSpecifiers(std::span<const Token> specifiers, Declaration& declaration) {
    for (const Token& token : specifiers) {
        if (isPunctuator(token, "*")) {
            ++declaration.pointerDepth;
        } else if (isIdentifier(token, "extern")) {
            declaration.isExtern = true;
        } else if (token.kind == TokenKind::Identifier && !isQualifier(token.text)) {
            declaration.type = token.text;
        }
    }
    if (declaration.type.empty()) {
        declaration.type = specifiers.back().text;
    }
}

void readInitializer(std::span<const Token> initializer, Declaration& declaration) {
    declaration.hasInitializer = true;
    declaration.initializer = initializer;
    const std::optional<std::span<const Token>> body = braceBody(initializer);
    if (!body) {
        return;
    }
    declaration.initializer = *body;
    const bool fromImage =
        std::ranges::any_of(splitElements(*body), [](std::span<const Token> element) {
            return element.size() == 1 && element.front().kind == TokenKind::Image;
        });
    if (!declaration.arrayLength && declaration.isArray && !fromImage) {
        declaration.arrayLength = countElements(*body);
    }
}

class Parser {
public:
    Parser(const TranslationUnit& unit, const EvalContext& context)
        : tokens(unit.tokens), macros(unit.macros), context(context) {}

    [[nodiscard]] ParseResult run();

private:
    [[nodiscard]] bool endsAt(usize cursor, std::string_view punctuator) const {
        return cursor < tokens.size() && isPunctuator(tokens[cursor], punctuator);
    }

    [[nodiscard]] usize findStatementEnd(usize begin, bool& isFunction) const;

    [[nodiscard]] usize findTopLevel(usize begin, std::span<const std::string_view> stops) const;

    [[nodiscard]] usize skipBalanced(usize open) const;

    [[nodiscard]] usize readDeclarationList(std::span<const Token> head, usize stop);

    [[nodiscard]] std::optional<std::vector<Token>>
    readDeclaration(std::span<const Token> rawHead,
                    std::optional<std::span<const Token>> initializer);

    void readArray(std::span<const Token> head, usize bracket, Declaration& declaration);

    [[nodiscard]] std::optional<usize> evaluateLength(std::span<const Token> bracket);

    void skipped(const Token& at, std::string_view why) {
        result.diagnostics.push_back(note(locate(at), std::format("{}, so it is skipped", why)));
    }

    std::span<const Token> tokens;
    const MacroTable& macros;
    const EvalContext& context;
    std::unordered_map<std::string_view, usize> lengths;
    ParseResult result;
};

usize Parser::findStatementEnd(usize begin, bool& isFunction) const {
    int nesting = 0;
    bool sawParentheses = false;
    for (usize cursor = begin; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile) {
            return cursor;
        }
        if (nesting == 0 && isPunctuator(token, "(")) {
            sawParentheses = true;
        }
        if (nesting == 0 && isPunctuator(token, "{") && sawParentheses) {
            isFunction = true;
            return cursor;
        }
        nesting += nestingDelta(token);
        if (nesting == 0 &&
            (isPunctuator(token, ";") || isPunctuator(token, "=") || isPunctuator(token, ","))) {
            return cursor;
        }
    }
    return tokens.size();
}

usize Parser::findTopLevel(usize begin, std::span<const std::string_view> stops) const {
    int nesting = 0;
    usize cursor = begin;
    while (cursor < tokens.size() && tokens[cursor].kind != TokenKind::EndOfFile) {
        if (nesting == 0 && std::ranges::any_of(stops, [&](std::string_view stop) {
                return isPunctuator(tokens[cursor], stop);
            })) {
            break;
        }
        nesting += nestingDelta(tokens[cursor]);
        ++cursor;
    }
    return cursor;
}

usize Parser::skipBalanced(usize open) const {
    int nesting = 0;
    usize cursor = open;
    do {
        nesting += nestingDelta(tokens[cursor]);
        ++cursor;
    } while (cursor < tokens.size() && nesting != 0);
    return cursor;
}

std::optional<usize> Parser::evaluateLength(std::span<const Token> bracket) {
    if (bracket.empty()) {
        return std::nullopt;
    }

    EvalContext nested = context;
    nested.macros = &macros;
    nested.unknownIsZero = false;
    nested.arrayLength = [this](std::string_view name) -> std::optional<usize> {
        const auto found = lengths.find(name);
        if (found != lengths.end()) {
            return found->second;
        }
        return context.arrayLength ? context.arrayLength(name) : std::nullopt;
    };

    const usize reported = result.diagnostics.size();
    const std::expected<Value, Diagnostic> length = evaluate(bracket, nested, result.diagnostics);
    if (!length) {
        result.diagnostics.push_back(length.error());
        return std::nullopt;
    }
    const bool unresolved =
        std::any_of(result.diagnostics.begin() + static_cast<std::ptrdiff_t>(reported),
                    result.diagnostics.end(), [](const Diagnostic& diagnostic) {
                        return diagnostic.severity != Severity::Note;
                    });
    const s64 count = asInteger(*length);
    if (unresolved || count < 0) {
        return std::nullopt;
    }
    return static_cast<usize>(count);
}

void Parser::readArray(std::span<const Token> head, usize bracket, Declaration& declaration) {
    usize close = bracket + 1;
    int nesting = 0;
    while (close < head.size() && (nesting != 0 || !isPunctuator(head[close], "]"))) {
        nesting += nestingDelta(head[close]);
        ++close;
    }
    declaration.arrayLength = evaluateLength(head.subspan(bracket + 1, close - bracket - 1));
}

std::optional<std::vector<Token>>
Parser::readDeclaration(std::span<const Token> rawHead,
                        std::optional<std::span<const Token>> initializer) {
    const std::vector<Token> kept = withoutAttributes(rawHead);
    const std::span<const Token> head(kept);
    const usize boundary = static_cast<usize>(
        std::ranges::find_if(head,
                             [](const Token& token) {
                                 return isPunctuator(token, "[") || isPunctuator(token, "(");
                             }) -
        head.begin());

    const bool parenthesized = boundary < head.size() && isPunctuator(head[boundary], "(");
    if (parenthesized && boundary + 1 < head.size() && isPunctuator(head[boundary + 1], "*")) {
        skipped(head[boundary], "cannot read a function pointer declaration");
        return std::nullopt;
    }
    const std::span<const Token> declarator = head.subspan(0, boundary);
    const usize nameAt = findNameIn(declarator);
    if (nameAt == declarator.size() || nameAt == 0) {
        skipped(rawHead.front(), "cannot read a declaration here");
        return std::nullopt;
    }
    if (parenthesized) {
        if (boundary != nameAt + 1) {
            skipped(head[boundary], "cannot read a declaration here");
        }
        return std::nullopt;
    }

    Declaration declaration;
    declaration.name = declarator[nameAt].text;
    declaration.location = locate(declarator[nameAt]);
    declaration.isArray = boundary != head.size();
    readSpecifiers(declarator.subspan(0, nameAt), declaration);
    if (declaration.isArray) {
        readArray(head, boundary, declaration);
    }
    if (initializer) {
        readInitializer(*initializer, declaration);
    }

    if (declaration.arrayLength) {
        lengths.insert_or_assign(declaration.name, *declaration.arrayLength);
    }
    result.declarations.push_back(declaration);

    std::vector<Token> specifiers;
    for (const Token& token : declarator.subspan(0, nameAt)) {
        if (!isPunctuator(token, "*")) {
            specifiers.push_back(token);
        }
    }
    return specifiers;
}

usize Parser::readDeclarationList(std::span<const Token> head, usize stop) {
    static constexpr std::array InitializerStops = {std::string_view(";"), std::string_view(",")};
    static constexpr std::array DeclaratorStops = {std::string_view(";"), std::string_view(","),
                                                   std::string_view("=")};
    static constexpr std::array StatementStops = {std::string_view(";")};

    std::vector<Token> declarator(head.begin(), head.end());
    std::optional<std::vector<Token>> specifiers;
    while (true) {
        usize end = stop;
        std::optional<std::span<const Token>> initializer;
        if (endsAt(stop, "=")) {
            end = findTopLevel(stop + 1, InitializerStops);
            initializer = tokens.subspan(stop + 1, end - stop - 1);
        }

        std::optional<std::vector<Token>> read = readDeclaration(declarator, initializer);
        if (!specifiers) {
            if (!read) {
                return findTopLevel(end, StatementStops);
            }
            specifiers = std::move(read);
        }
        if (!endsAt(end, ",")) {
            if (!endsAt(end, ";")) {
                skipped(head.front(), "a declaration is never closed with ';'");
            }
            return end;
        }

        stop = findTopLevel(end + 1, DeclaratorStops);
        declarator = *specifiers;
        declarator.insert(declarator.end(), tokens.begin() + static_cast<std::ptrdiff_t>(end + 1),
                          tokens.begin() + static_cast<std::ptrdiff_t>(stop));
    }
}

ParseResult Parser::run() {
    static constexpr std::array StatementStops = {std::string_view(";")};

    usize cursor = 0;
    while (cursor < tokens.size() && tokens[cursor].kind != TokenKind::EndOfFile) {
        if (isPunctuator(tokens[cursor], ";")) {
            ++cursor;
            continue;
        }

        bool isFunction = false;
        const usize stop = findStatementEnd(cursor, isFunction);
        if (stop >= tokens.size() || tokens[stop].kind == TokenKind::EndOfFile) {
            skipped(tokens[cursor], "a declaration is never closed with ';'");
            break;
        }
        if (isFunction) {
            cursor = skipBalanced(stop);
            continue;
        }

        const std::span<const Token> head = tokens.subspan(cursor, stop - cursor);
        if (head.empty() || std::ranges::find(Skipped, head.front().text) != Skipped.end()) {
            cursor = findTopLevel(stop, StatementStops) + 1;
            continue;
        }
        cursor = readDeclarationList(head, stop) + 1;
    }
    return std::move(result);
}

} // namespace

ParseResult parseDeclarations(const TranslationUnit& unit, const EvalContext& context) {
    return Parser(unit, context).run();
}

} // namespace z64
