#include "Preprocessor.h"

#include "base/Diagnostic.h"
#include "cparser/Expand.h"
#include "cparser/Expression.h"
#include "cparser/IncludeResolver.h"
#include "cparser/MacroTable.h"
#include "cparser/Scanner.h"
#include "cparser/SourceFile.h"
#include "cparser/SourceSet.h"
#include "cparser/Token.h"
#include "cparser/Types.h"
#include "cparser/Value.h"

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

namespace cparser {

namespace {

struct Conditional {
    bool parentActive = true;
    bool taken = false;
    bool active = false;
    base::SourceLocation opened;
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
        : unit(unit), options(options),
          resolver(options.searchPaths, options.allowedRoots, options.resolveMissingInclude) {}

    void run(const SourceFile& file, usize depth);

    void flushPending();

private:
    [[nodiscard]] bool isActive() const {
        return conditionals.empty() || conditionals.back().active;
    }

    [[nodiscard]] const std::vector<Token>& getTokens(const SourceFile& file);

    [[nodiscard]] EvalContext makeEvalContext() const;

    void applyDirective(std::span<const Token> line, usize depth);

    void doInclude(const Token& directive, std::span<const Token> line, usize depth);

    void doDefine(const Token& directive, std::span<const Token> line);

    void doConditional(const Token& directive, std::span<const Token> line);

    [[nodiscard]] bool evaluateCondition(const Token& directive, std::span<const Token> line);

    [[nodiscard]] std::vector<Token> substituteDefined(std::span<const Token> line);

    TranslationUnit& unit;
    const PreprocessOptions& options;
    IncludeResolver resolver;
    std::unordered_map<const SourceFile*, std::vector<Token>> scanned;
    std::vector<const SourceFile*> stack;
    std::vector<Conditional> conditionals;
    std::vector<Token> pending;
};

void Preprocessor::flushPending() {
    if (pending.empty()) {
        return;
    }
    const std::vector<Token> expanded =
        expand(pending, ExpandOptions{.macros = &unit.macros, .spellings = &unit.spellings},
               unit.diagnostics);
    unit.tokens.insert(unit.tokens.end(), expanded.begin(), expanded.end());
    pending.clear();
}

const std::vector<Token>& Preprocessor::getTokens(const SourceFile& file) {
    const auto found = scanned.find(&file);
    if (found != scanned.end()) {
        return found->second;
    }

    ScanResult result = scan(file);
    for (base::Diagnostic& diagnostic : result.diagnostics) {
        unit.diagnostics.push_back(std::move(diagnostic));
    }
    return scanned.emplace(&file, std::move(result.tokens)).first->second;
}

EvalContext Preprocessor::makeEvalContext() const {
    return EvalContext{
        .macros = &unit.macros,
        .dialect = options.dialect,
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
                base::makeWarning(locate(line[cursor]), "'defined' expects a macro name"));
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

bool Preprocessor::evaluateCondition(const Token& directive, std::span<const Token> line) {
    if (line.empty()) {
        unit.diagnostics.push_back(base::makeWarning(
            locate(directive),
            std::format("'#{}' has no condition, reading it as false", directive.text)));
        return false;
    }
    const std::vector<Token> prepared = substituteDefined(line);
    const std::expected<Value, base::Diagnostic> value =
        evaluate(prepared, makeEvalContext(), unit.diagnostics);
    if (!value) {
        unit.diagnostics.push_back(value.error());
        return false;
    }
    return getInteger(*value) != 0;
}

void Preprocessor::doConditional(const Token& directive, std::span<const Token> line) {
    const std::string_view name = directive.text;
    if (name == "endif") {
        if (conditionals.empty()) {
            unit.diagnostics.push_back(
                base::makeWarning(locate(directive), "'#endif' without a matching '#if'"));
            return;
        }
        conditionals.pop_back();
        return;
    }

    if (name == "if" || name == "ifdef" || name == "ifndef") {
        const bool parentActive = isActive();
        bool taken = false;
        if (parentActive) {
            if (name == "if") {
                taken = evaluateCondition(directive, line);
            } else if (!line.empty() && line.front().kind == TokenKind::Identifier) {
                taken = unit.macros.contains(line.front().text) == (name == "ifdef");
            } else {
                unit.diagnostics.push_back(base::makeWarning(
                    locate(directive), std::format("'#{}' expects a macro name", name)));
            }
        }
        conditionals.push_back(Conditional{.parentActive = parentActive,
                                           .taken = taken,
                                           .active = taken,
                                           .opened = locate(directive)});
        return;
    }

    if (conditionals.empty()) {
        unit.diagnostics.push_back(base::makeWarning(
            locate(directive), std::format("'#{}' without a matching '#if'", name)));
        return;
    }

    Conditional& state = conditionals.back();
    if (name == "else") {
        state.active = state.parentActive && !state.taken;
        state.taken = true;
        return;
    }

    const bool taken = state.parentActive && !state.taken && evaluateCondition(directive, line);
    state.active = taken;
    state.taken = state.taken || taken;
}

void Preprocessor::doDefine(const Token& directive, std::span<const Token> line) {
    std::optional<Macro> macro = parseDefinition(line);
    if (!macro) {
        unit.diagnostics.push_back(
            base::makeWarning(locate(directive), "'#define' expects a macro name"));
        return;
    }
    if (options.onDefine) {
        options.onDefine(*macro, directive);
    }
    unit.macros.define(std::move(*macro));
}

void Preprocessor::doInclude(const Token& directive, std::span<const Token> line, usize depth) {
    if (line.empty() || line.front().kind != TokenKind::String) {
        unit.diagnostics.push_back(base::makeWarning(
            locate(line.empty() ? directive : line.front()), "cannot read the include"));
        return;
    }

    const Token& target = line.front();
    const base::SourceLocation location = locate(target);
    if (target.text.starts_with('<')) {
        unit.diagnostics.push_back(base::makeNote(
            location, std::format("not following the system include {}", target.text)));
        return;
    }

    if (depth >= options.maxDepth) {
        unit.diagnostics.push_back(base::makeWarning(
            location, std::format("not following {}: {} includes deep", target.text, depth)));
        return;
    }

    const std::string spelling(unquote(target.text));
    const std::expected<ResolvedInclude, std::string> resolved =
        resolver.resolve(spelling, target.file->path.parent_path());
    if (!resolved) {
        unit.diagnostics.push_back(base::makeNote(location, resolved.error()));
        if (std::ranges::find(unit.unresolvedIncludes, spelling) == unit.unresolvedIncludes.end()) {
            unit.unresolvedIncludes.push_back(spelling);
        }
        return;
    }

    if (resolved->embedded) {
        unit.tokens.push_back(Token{
            .kind = TokenKind::Embedded,
            .startsLine = true,
            .spaceBefore = true,
            .embedded = static_cast<u32>(unit.embedded.size()),
            .file = target.file,
            .text = target.text,
            .line = target.line,
            .column = target.column,
        });
        unit.embedded.push_back(
            EmbeddedFile{.path = resolved->path, .tag = resolved->tag, .location = location});
        return;
    }

    const std::expected<const SourceFile*, base::Diagnostic> file =
        unit.sources.open(resolved->path);
    if (!file) {
        unit.diagnostics.push_back(
            base::makeWarning(location, std::format("cannot read {}: {}", resolved->path.string(),
                                                    file.error().message)));
        return;
    }
    if (std::ranges::find(stack, *file) != stack.end()) {
        unit.diagnostics.push_back(base::makeNote(
            location, std::format("not following {}: it is already open", spelling)));
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
        doConditional(line.front(), rest);
        return;
    }
    if (!isActive()) {
        return;
    }
    if (directive == "include") {
        doInclude(line.front(), rest, depth);
    } else if (directive == "define") {
        doDefine(line.front(), rest);
    } else if (directive == "undef") {
        if (!rest.empty()) {
            unit.macros.remove(rest.front().text);
        }
    } else if (directive == "error" || directive == "warning") {
        unit.diagnostics.push_back(base::makeWarning(
            locate(line.front()), std::format("the source says #{} here", directive)));
    } else if (directive != "pragma") {
        unit.diagnostics.push_back(
            base::makeNote(locate(line.front()), std::format("ignoring '#{}'", directive)));
    }
}

void Preprocessor::run(const SourceFile& file, usize depth) {
    stack.push_back(&file);
    const usize conditionalsOnEntry = conditionals.size();
    const std::vector<Token>& tokens = getTokens(file);

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
            flushPending();
            applyDirective(std::span(tokens).subspan(cursor + 1, end - cursor - 1), depth);
            cursor = end - 1;
            continue;
        }

        if (isActive()) {
            if (options.expandMacros) {
                pending.push_back(token);
            } else {
                unit.tokens.push_back(token);
            }
        }
    }

    for (usize open = conditionalsOnEntry; open < conditionals.size(); ++open) {
        unit.diagnostics.push_back(base::makeWarning(
            conditionals[open].opened, "this conditional is never closed in its file"));
    }
    conditionals.resize(conditionalsOnEntry);
    stack.pop_back();
}

void preprocessInto(TranslationUnit& unit, const SourceFile& entry,
                    const PreprocessOptions& options) {
    Preprocessor preprocessor(unit, options);
    preprocessor.run(entry, 0);
    preprocessor.flushPending();
    unit.tokens.push_back(Token{.kind = TokenKind::EndOfFile, .startsLine = true, .file = &entry});
}

} // namespace

std::expected<TranslationUnit, base::Diagnostic> preprocess(const MacroTable& base,
                                                            const std::filesystem::path& entry,
                                                            const PreprocessOptions& options) {
    TranslationUnit unit;
    unit.macros = MacroTable(&base);
    const std::expected<const SourceFile*, base::Diagnostic> file = unit.sources.open(entry);
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

} // namespace cparser
