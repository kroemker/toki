#include "Preprocessor.h"

#include "z64/Diagnostic.h"
#include "z64/Expression.h"
#include "z64/IncludeResolver.h"
#include "z64/MacroTable.h"
#include "z64/Scanner.h"
#include "z64/SourceFile.h"
#include "z64/SourceSet.h"
#include "z64/Token.h"
#include "z64/Types.h"
#include "z64/Value.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace z64 {

namespace {

struct Conditional {
    bool parentActive = true;
    bool taken = false;
    bool active = false;
};

[[nodiscard]] std::string_view unquote(std::string_view spelling) {
    if (spelling.size() >= 2 && (spelling.front() == '"' || spelling.front() == '<')) {
        return spelling.substr(1, spelling.size() - 2);
    }
    return spelling;
}

class Preprocessor {
public:
    Preprocessor(TranslationUnit& unit, const PreprocessOptions& options)
        : unit(unit), options(options), resolver(options.searchPaths, options.allowedRoots) {}

    void run(const SourceFile& file, usize depth);

private:
    [[nodiscard]] bool active() const {
        return conditionals.empty() || conditionals.back().active;
    }

    [[nodiscard]] const std::vector<Token>& tokensOf(const SourceFile& file);

    [[nodiscard]] EvalContext evalContext() const;

    void applyDirective(std::span<const Token> line, usize depth);

    void doInclude(std::span<const Token> line, usize depth);

    void doDefine(std::span<const Token> line);

    void doConditional(std::string_view directive, std::span<const Token> line);

    [[nodiscard]] bool evaluateCondition(std::span<const Token> line);

    [[nodiscard]] std::vector<Token> substituteDefined(std::span<const Token> line);

    TranslationUnit& unit;
    const PreprocessOptions& options;
    IncludeResolver resolver;
    std::unordered_map<const SourceFile*, std::vector<Token>> scanned;
    std::vector<const SourceFile*> stack;
    std::vector<Conditional> conditionals;
};

const std::vector<Token>& Preprocessor::tokensOf(const SourceFile& file) {
    const auto found = scanned.find(&file);
    if (found != scanned.end()) {
        return found->second;
    }

    ScanResult result = scan(file);
    for (Diagnostic& diagnostic : result.diagnostics) {
        unit.diagnostics.push_back(std::move(diagnostic));
    }
    return scanned.emplace(&file, std::move(result.tokens)).first->second;
}

EvalContext Preprocessor::evalContext() const {
    return EvalContext{
        .macros = &unit.macros,
        .resolve = options.resolve,
        .unknownIsZero = !options.strictConditionals,
    };
}

std::vector<Token> Preprocessor::substituteDefined(std::span<const Token> line) {
    static constexpr std::string_view True = "1";
    static constexpr std::string_view False = "0";

    std::vector<Token> output;
    for (usize cursor = 0; cursor < line.size(); ++cursor) {
        if (!isIdentifier(line[cursor], "defined")) {
            output.push_back(line[cursor]);
            continue;
        }

        usize at = cursor + 1;
        const bool parenthesized = at < line.size() && isPunctuator(line[at], "(");
        if (parenthesized) {
            ++at;
        }
        if (at >= line.size() || line[at].kind != TokenKind::Identifier) {
            unit.diagnostics.push_back(
                warning(locate(line[cursor]), "'defined' expects a macro name"));
            continue;
        }

        Token replacement = line[cursor];
        replacement.kind = TokenKind::Integer;
        replacement.text = unit.macros.contains(line[at].text) ? True : False;
        output.push_back(replacement);

        cursor = at;
        if (parenthesized && cursor + 1 < line.size() && isPunctuator(line[cursor + 1], ")")) {
            ++cursor;
        }
    }
    return output;
}

bool Preprocessor::evaluateCondition(std::span<const Token> line) {
    const std::vector<Token> prepared = substituteDefined(line);
    const std::expected<Value, Diagnostic> value =
        evaluate(prepared, evalContext(), unit.diagnostics);
    if (!value) {
        unit.diagnostics.push_back(value.error());
        return false;
    }
    return asInteger(*value) != 0;
}

void Preprocessor::doConditional(std::string_view directive, std::span<const Token> line) {
    if (directive == "endif") {
        if (conditionals.empty()) {
            unit.diagnostics.push_back(
                warning(SourceLocation{}, "'#endif' without a matching '#if'"));
            return;
        }
        conditionals.pop_back();
        return;
    }

    if (directive == "if" || directive == "ifdef" || directive == "ifndef") {
        const bool parentActive = active();
        bool taken = false;
        if (parentActive) {
            if (directive == "if") {
                taken = evaluateCondition(line);
            } else if (!line.empty() && line.front().kind == TokenKind::Identifier) {
                taken = unit.macros.contains(line.front().text) == (directive == "ifdef");
            } else {
                unit.diagnostics.push_back(warning(
                    SourceLocation{}, std::format("'#{}' expects a macro name", directive)));
            }
        }
        conditionals.push_back(
            Conditional{.parentActive = parentActive, .taken = taken, .active = taken});
        return;
    }

    if (conditionals.empty()) {
        unit.diagnostics.push_back(
            warning(SourceLocation{}, std::format("'#{}' without a matching '#if'", directive)));
        return;
    }

    Conditional& state = conditionals.back();
    if (directive == "else") {
        state.active = state.parentActive && !state.taken;
        state.taken = true;
        return;
    }

    const bool taken = state.parentActive && !state.taken && evaluateCondition(line);
    state.active = taken;
    state.taken = state.taken || taken;
}

void Preprocessor::doDefine(std::span<const Token> line) {
    std::optional<Macro> macro = parseDefinition(line);
    if (!macro) {
        unit.diagnostics.push_back(warning(SourceLocation{}, "'#define' expects a macro name"));
        return;
    }
    unit.macros.define(std::move(*macro));
}

void Preprocessor::doInclude(std::span<const Token> line, usize depth) {
    if (line.empty() || line.front().kind != TokenKind::String) {
        unit.diagnostics.push_back(warning(line.empty() ? SourceLocation{} : locate(line.front()),
                                           "cannot read the include"));
        return;
    }

    const Token& target = line.front();
    const SourceLocation location = locate(target);
    if (target.text.starts_with('<')) {
        unit.diagnostics.push_back(
            note(location, std::format("not following the system include {}", target.text)));
        return;
    }

    if (depth >= options.maxDepth) {
        unit.diagnostics.push_back(warning(
            location, std::format("not following {}: {} includes deep", target.text, depth)));
        return;
    }

    const std::string spelling(unquote(target.text));
    const std::expected<ResolvedInclude, std::string> resolved =
        resolver.resolve(spelling, target.file->path.parent_path());
    if (!resolved) {
        unit.diagnostics.push_back(note(location, resolved.error()));
        return;
    }

    if (resolved->isImage) {
        unit.images.push_back(ImageReference{
            .path = resolved->path, .format = resolved->format, .location = location});
        return;
    }

    const std::expected<const SourceFile*, Diagnostic> file = unit.sources.open(resolved->path);
    if (!file) {
        unit.diagnostics.push_back(
            warning(location, std::format("cannot read {}: {}", resolved->path.string(),
                                          file.error().message)));
        return;
    }
    if (std::ranges::find(stack, *file) != stack.end()) {
        unit.diagnostics.push_back(
            note(location, std::format("not following {}: it is already open", spelling)));
        return;
    }
    run(**file, depth + 1);
}

void Preprocessor::applyDirective(std::span<const Token> line, usize depth) {
    if (line.empty()) {
        return;
    }

    const std::string_view directive = line.front().text;
    const std::span<const Token> rest = line.subspan(1);
    if (directive == "if" || directive == "ifdef" || directive == "ifndef" || directive == "elif" ||
        directive == "else" || directive == "endif") {
        doConditional(directive, rest);
        return;
    }
    if (!active()) {
        return;
    }
    if (directive == "include") {
        doInclude(rest, depth);
    } else if (directive == "define") {
        doDefine(rest);
    } else if (directive == "undef") {
        if (!rest.empty()) {
            unit.macros.remove(rest.front().text);
        }
    } else if (directive == "error" || directive == "warning") {
        unit.diagnostics.push_back(
            warning(locate(line.front()), std::format("the source says #{} here", directive)));
    } else if (directive != "pragma") {
        unit.diagnostics.push_back(
            note(locate(line.front()), std::format("ignoring '#{}'", directive)));
    }
}

void Preprocessor::run(const SourceFile& file, usize depth) {
    stack.push_back(&file);
    const usize conditionalsOnEntry = conditionals.size();
    const std::vector<Token>& tokens = tokensOf(file);

    for (usize cursor = 0; cursor < tokens.size(); ++cursor) {
        const Token& token = tokens[cursor];
        if (token.kind == TokenKind::EndOfFile) {
            break;
        }

        if (token.startsLine && isPunctuator(token, "#")) {
            usize end = cursor + 1;
            while (end < tokens.size() && !tokens[end].startsLine &&
                   tokens[end].kind != TokenKind::EndOfFile) {
                ++end;
            }
            applyDirective(std::span(tokens).subspan(cursor + 1, end - cursor - 1), depth);
            cursor = end - 1;
            continue;
        }

        if (active()) {
            unit.tokens.push_back(token);
        }
    }

    if (conditionals.size() != conditionalsOnEntry) {
        unit.diagnostics.push_back(
            warning(SourceLocation{.file = file.path}, "a conditional is left unclosed"));
        conditionals.resize(conditionalsOnEntry);
    }
    stack.pop_back();
}

void preprocessInto(TranslationUnit& unit, const SourceFile& entry,
                    const PreprocessOptions& options) {
    Preprocessor(unit, options).run(entry, 0);
    unit.tokens.push_back(Token{.kind = TokenKind::EndOfFile, .startsLine = true, .file = &entry});
}

} // namespace

std::expected<TranslationUnit, Diagnostic> preprocess(const MacroTable& base,
                                                      const std::filesystem::path& entry,
                                                      const PreprocessOptions& options) {
    TranslationUnit unit;
    unit.macros = MacroTable(&base);
    const std::expected<const SourceFile*, Diagnostic> file = unit.sources.open(entry);
    if (!file) {
        return std::unexpected(file.error());
    }
    preprocessInto(unit, **file, options);
    return unit;
}

TranslationUnit preprocessText(const MacroTable& base, const std::filesystem::path& path,
                               std::string contents, const PreprocessOptions& options) {
    TranslationUnit unit;
    unit.macros = MacroTable(&base);
    preprocessInto(unit, *unit.sources.add(path, std::move(contents)), options);
    return unit;
}

} // namespace z64
