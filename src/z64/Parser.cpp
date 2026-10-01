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
#include <expected>
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

class Parser {
public:
    Parser(const TranslationUnit& unit, const EvalContext& context)
        : tokens(unit.tokens), macros(unit.macros), context(context) {}

    [[nodiscard]] ParseResult run();

private:
    [[nodiscard]] usize findStatementEnd(usize begin, bool& isFunction) const;

    [[nodiscard]] usize skipBalanced(usize open) const;

    [[nodiscard]] usize findInitializerEnd(usize begin) const;

    void readDeclaration(std::span<const Token> head, std::span<const Token> initializer,
                         bool hasInitializer);

    [[nodiscard]] std::optional<usize> evaluateLength(std::span<const Token> bracket);

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
        if (nesting == 0 && (isPunctuator(token, ";") || isPunctuator(token, "="))) {
            return cursor;
        }
    }
    return tokens.size();
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

usize Parser::findInitializerEnd(usize begin) const {
    int nesting = 0;
    usize cursor = begin;
    while (cursor < tokens.size() && tokens[cursor].kind != TokenKind::EndOfFile) {
        if (nesting == 0 && isPunctuator(tokens[cursor], ";")) {
            break;
        }
        nesting += nestingDelta(tokens[cursor]);
        ++cursor;
    }
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

    const std::expected<Value, Diagnostic> length = evaluate(bracket, nested, result.diagnostics);
    if (!length) {
        result.diagnostics.push_back(length.error());
        return std::nullopt;
    }
    const s64 count = asInteger(*length);
    return count < 0 ? std::nullopt : std::optional<usize>(static_cast<usize>(count));
}

usize findNameIn(std::span<const Token> declarator) {
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

void Parser::readDeclaration(std::span<const Token> head, std::span<const Token> initializer,
                             bool hasInitializer) {
    const usize bracket = static_cast<usize>(
        std::ranges::find_if(head, [](const Token& token) { return isPunctuator(token, "["); }) -
        head.begin());

    const std::span<const Token> declarator = head.subspan(0, bracket);
    const usize nameAt = findNameIn(declarator);
    if (nameAt == declarator.size() || nameAt == 0) {
        return;
    }

    Declaration declaration;
    declaration.name = declarator[nameAt].text;
    declaration.location = locate(declarator[nameAt]);
    declaration.hasInitializer = hasInitializer;
    declaration.isArray = bracket != head.size();
    readSpecifiers(declarator.subspan(0, nameAt), declaration);

    if (declaration.isArray) {
        usize close = bracket + 1;
        int nesting = 0;
        while (close < head.size() && (nesting != 0 || !isPunctuator(head[close], "]"))) {
            nesting += nestingDelta(head[close]);
            ++close;
        }
        declaration.arrayLength = evaluateLength(head.subspan(bracket + 1, close - bracket - 1));
    }

    if (hasInitializer) {
        declaration.initializer = initializer;
        if (const std::optional<std::span<const Token>> body = braceBody(initializer)) {
            declaration.initializer = *body;
            if (!declaration.arrayLength && declaration.isArray) {
                declaration.arrayLength = countElements(*body);
            }
        }
    }

    if (declaration.arrayLength) {
        lengths.insert_or_assign(declaration.name, *declaration.arrayLength);
    }
    result.declarations.push_back(declaration);
}

ParseResult Parser::run() {
    usize cursor = 0;
    while (cursor < tokens.size() && tokens[cursor].kind != TokenKind::EndOfFile) {
        if (isPunctuator(tokens[cursor], ";")) {
            ++cursor;
            continue;
        }

        bool isFunction = false;
        const usize stop = findStatementEnd(cursor, isFunction);
        if (stop >= tokens.size() || tokens[stop].kind == TokenKind::EndOfFile) {
            break;
        }

        const std::span<const Token> head = tokens.subspan(cursor, stop - cursor);
        const bool skip = isFunction || head.empty() ||
                          std::ranges::find(Skipped, head.front().text) != Skipped.end();

        if (isPunctuator(tokens[stop], "{")) {
            cursor = skipBalanced(stop);
            continue;
        }

        if (isPunctuator(tokens[stop], ";")) {
            if (!skip) {
                readDeclaration(head, {}, false);
            }
            cursor = stop + 1;
            continue;
        }

        const usize end = findInitializerEnd(stop + 1);
        if (!skip) {
            readDeclaration(head, tokens.subspan(stop + 1, end - stop - 1), true);
        }
        cursor = end + 1;
    }
    return std::move(result);
}

} // namespace

ParseResult parseDeclarations(const TranslationUnit& unit, const EvalContext& context) {
    return Parser(unit, context).run();
}

} // namespace z64
