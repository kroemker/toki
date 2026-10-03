#include "Makefile.h"

#include "base/Diagnostic.h"
#include "makeparser/MakeSyntax.h"
#include "makeparser/MakeVariables.h"
#include "makeparser/MakeWords.h"
#include "makeparser/Types.h"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace makeparser {

namespace {

constexpr usize MaxIncludeDepth = 16;

constexpr std::array ConditionalKeywords = {
    std::string_view("ifeq"),   std::string_view("ifneq"), std::string_view("ifdef"),
    std::string_view("ifndef"), std::string_view("else"),  std::string_view("endif"),
};

struct Condition {
    bool value = false;
    bool certain = true;
};

struct Definition {
    std::string name;
    MakeAssign assign = MakeAssign::Recursive;
    bool isOverride = false;
    bool active = false;
    u32 line = 0;
    usize nesting = 0;
    std::vector<std::string> body;
};

[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

[[nodiscard]] bool extendDefinition(Definition& definition, const MakeLine& line) {
    const std::string_view keyword = splitFirstWord(line.text).first;
    if (keyword == "define") {
        ++definition.nesting;
    } else if (keyword == "endef") {
        if (definition.nesting == 0) {
            return true;
        }
        --definition.nesting;
    }
    definition.body.push_back(line.text);
    return false;
}

class Reader {
public:
    Reader(std::filesystem::path directory, Makefile& makefile)
        : directory(std::move(directory)), makefile(makefile) {}

    void readFile(const std::filesystem::path& path, usize depth);

private:
    struct Conditional {
        bool parentActive = true;
        bool taken = false;
        bool active = false;
        bool certain = true;
    };

    [[nodiscard]] bool isActive() const {
        return conditionals.empty() || conditionals.back().active;
    }

    [[nodiscard]] bool isBranchCertain() const {
        return std::ranges::all_of(
            conditionals, [](const Conditional& conditional) { return conditional.certain; });
    }

    [[nodiscard]] MakeSite makeSite(u32 line) const {
        return MakeSite{.location =
                            base::SourceLocation{.file = currentFile, .line = line, .column = 1},
                        .certain = isBranchCertain()};
    }

    [[nodiscard]] MakeText expand(std::string_view text, u32 line) {
        return makefile.variables.expand(text, makeSite(line), makefile.diagnostics);
    }

    void handle(const MakeLine& line, usize depth);

    void handleConditional(std::string_view keyword, std::string_view rest, u32 line);

    [[nodiscard]] Condition evaluate(std::string_view keyword, std::string_view rest, u32 line);

    void handleInclude(std::string_view rest, bool optional, u32 line, usize depth);

    void handleStatement(std::string_view text, bool isOverride, u32 line);

    void assign(const std::string& name, MakeAssign kind, std::string_view value, bool isOverride,
                u32 line);

    void startDefinition(std::string_view rest, bool isOverride, u32 line);

    void finishDefinition(const Definition& finished);

    std::filesystem::path directory;
    Makefile& makefile;
    std::filesystem::path currentFile;
    std::vector<Conditional> conditionals;
    std::optional<Definition> definition;
    bool inRule = false;
};

void Reader::readFile(const std::filesystem::path& path, usize depth) {
    const std::optional<std::string> text = readText(path);
    if (!text) {
        makefile.diagnostics.push_back(
            base::makeWarning(base::SourceLocation{.file = path}, "cannot read the file"));
        return;
    }

    const std::filesystem::path includingFile = std::exchange(currentFile, path);
    const bool includingInRule = std::exchange(inRule, false);
    const usize conditionalsOnEntry = conditionals.size();

    for (const MakeLine& line : splitMakeLines(*text)) {
        if (!definition) {
            handle(line, depth);
        } else if (extendDefinition(*definition, line)) {
            const Definition finished = std::move(*definition);
            definition.reset();
            finishDefinition(finished);
        }
    }

    if (definition) {
        makefile.diagnostics.push_back(base::makeWarning(
            makeSite(definition->line).location,
            std::format("'define {}' is never closed with 'endef'", definition->name)));
        definition.reset();
    }
    if (conditionals.size() != conditionalsOnEntry) {
        makefile.diagnostics.push_back(base::makeWarning(base::SourceLocation{.file = path},
                                                         "a conditional is left unclosed"));
        conditionals.resize(conditionalsOnEntry);
    }
    currentFile = includingFile;
    inRule = includingInRule;
}

void Reader::handle(const MakeLine& line, usize depth) {
    if (line.recipe && inRule) {
        return;
    }
    const std::string_view text = trim(stripMakeComment(line.text));
    if (text.empty()) {
        return;
    }

    std::pair<std::string_view, std::string_view> words = splitFirstWord(text);
    if (std::ranges::find(ConditionalKeywords, words.first) != ConditionalKeywords.end()) {
        handleConditional(words.first, words.second, line.number);
        return;
    }

    std::string_view statement = text;
    bool isOverride = false;
    while (words.first == "export" || words.first == "override" || words.first == "private") {
        isOverride = isOverride || words.first == "override";
        statement = words.second;
        words = splitFirstWord(words.second);
    }
    const auto [keyword, rest] = words;
    if (keyword == "define") {
        startDefinition(rest, isOverride, line.number);
        return;
    }
    if (statement.empty() || !isActive()) {
        return;
    }

    inRule = false;
    if (keyword == "include" || keyword == "-include" || keyword == "sinclude") {
        handleInclude(rest, keyword != "include", line.number, depth);
    } else if (keyword == "undefine") {
        const std::string name = stripWords(expand(rest, line.number).text);
        const MakeVariable* found = makefile.variables.find(name);
        if (found != nullptr && (isOverride || found->origin == MakeOrigin::File)) {
            makefile.variables.remove(name);
        }
    } else if (keyword != "vpath" && keyword != "unexport") {
        handleStatement(statement, isOverride, line.number);
    }
}

void Reader::handleStatement(std::string_view text, bool isOverride, u32 line) {
    const MakeStatement statement = classifyStatement(text);
    switch (statement.kind) {
    case MakeStatementKind::Assignment: {
        const std::string name = stripWords(expand(statement.name, line).text);
        if (name.empty() || name.find_first_of(" \t") != std::string::npos) {
            makefile.diagnostics.push_back(base::makeNote(
                makeSite(line).location, std::format("cannot read '{}' as a variable name", name)));
            return;
        }
        assign(name, statement.assign, statement.value, isOverride, line);
        return;
    }
    case MakeStatementKind::Rule:
        inRule = true;
        return;
    case MakeStatementKind::Other:
        if (!trim(expand(text, line).text).empty()) {
            makefile.diagnostics.push_back(base::makeNote(
                makeSite(line).location, "neither an assignment nor a rule, so it is skipped"));
        }
        return;
    }
}

void Reader::handleConditional(std::string_view keyword, std::string_view rest, u32 line) {
    if (keyword == "endif") {
        if (conditionals.empty()) {
            makefile.diagnostics.push_back(
                base::makeWarning(makeSite(line).location, "'endif' without a matching 'if'"));
            return;
        }
        conditionals.pop_back();
        return;
    }

    if (keyword != "else") {
        Conditional conditional{.parentActive = isActive()};
        if (conditional.parentActive) {
            const Condition condition = evaluate(keyword, rest, line);
            conditional.taken = condition.value;
            conditional.active = condition.value;
            conditional.certain = condition.certain;
        } else {
            conditional.taken = true;
        }
        conditionals.push_back(conditional);
        return;
    }

    if (conditionals.empty()) {
        makefile.diagnostics.push_back(
            base::makeWarning(makeSite(line).location, "'else' without a matching 'if'"));
        return;
    }
    Conditional& top = conditionals.back();
    const auto [chained, chainedRest] = splitFirstWord(rest);
    if (chained.empty()) {
        top.active = top.parentActive && !top.taken;
        top.taken = true;
        return;
    }
    if (!top.parentActive || top.taken) {
        top.active = false;
        return;
    }
    const Condition condition = evaluate(chained, chainedRest, line);
    top.active = condition.value;
    top.taken = condition.value;
    top.certain = top.certain && condition.certain;
}

Condition Reader::evaluate(std::string_view keyword, std::string_view rest, u32 line) {
    if (keyword == "ifdef" || keyword == "ifndef") {
        const std::string name = stripWords(expand(rest, line).text);
        const MakeVariable* found = makefile.variables.find(name);
        const bool defined = found != nullptr && !found->value.empty();
        return Condition{.value = defined == (keyword == "ifdef"),
                         .certain = found == nullptr || found->certain};
    }
    if (keyword != "ifeq" && keyword != "ifneq") {
        makefile.diagnostics.push_back(base::makeWarning(
            makeSite(line).location, std::format("'{}' is not a conditional make knows", keyword)));
        return Condition{.value = false, .certain = false};
    }

    const auto operands = parseComparisonOperands(rest);
    if (!operands) {
        makefile.diagnostics.push_back(base::makeWarning(
            makeSite(line).location, std::format("cannot read the condition '{}'", rest)));
        return Condition{.value = false, .certain = false};
    }
    const MakeText left = expand(operands->first, line);
    const MakeText right = expand(operands->second, line);
    return Condition{.value = (left.text == right.text) == (keyword == "ifeq"),
                     .certain = left.certain && right.certain};
}

void Reader::handleInclude(std::string_view rest, bool optional, u32 line, usize depth) {
    const MakeText files = expand(rest, line);
    for (const std::string_view name : splitWords(files.text)) {
        const std::filesystem::path path = directory / std::filesystem::path(name);
        if (depth + 1 > MaxIncludeDepth) {
            makefile.diagnostics.push_back(
                base::makeWarning(makeSite(line).location,
                                  std::format("not including {}: includes nest too deep", name)));
            continue;
        }
        std::error_code failure;
        if (!std::filesystem::exists(path, failure)) {
            if (!optional) {
                makefile.diagnostics.push_back(base::makeWarning(
                    makeSite(line).location, std::format("cannot find the included {}", name)));
            }
            continue;
        }
        readFile(path, depth + 1);
    }
}

void Reader::assign(const std::string& name, MakeAssign kind, std::string_view value,
                    bool isOverride, u32 line) {
    const MakeVariable* existing = makefile.variables.find(name);
    if (existing != nullptr && !isOverride && existing->origin != MakeOrigin::File) {
        return;
    }
    const MakeOrigin origin = isOverride ? MakeOrigin::Override : MakeOrigin::File;
    const bool certain = isBranchCertain();

    switch (kind) {
    case MakeAssign::Conditional:
        if (existing != nullptr) {
            return;
        }
        [[fallthrough]];
    case MakeAssign::Recursive:
        makefile.variables.set(name, MakeVariable{.value = std::string(value),
                                                  .recursive = true,
                                                  .origin = origin,
                                                  .certain = certain});
        return;
    case MakeAssign::Simple: {
        MakeText expanded = expand(value, line);
        makefile.variables.set(name, MakeVariable{.value = std::move(expanded.text),
                                                  .recursive = false,
                                                  .origin = origin,
                                                  .certain = certain && expanded.certain});
        return;
    }
    case MakeAssign::Append: {
        if (existing == nullptr) {
            assign(name, MakeAssign::Recursive, value, isOverride, line);
            return;
        }
        MakeVariable appended = *existing;
        const MakeText expanded =
            appended.recursive ? MakeText{.text = std::string(value)} : expand(value, line);
        if (!appended.value.empty() && !expanded.text.empty()) {
            appended.value += ' ';
        }
        appended.value += expanded.text;
        appended.certain = appended.certain && certain && expanded.certain;
        appended.origin = isOverride ? MakeOrigin::Override : appended.origin;
        makefile.variables.set(name, std::move(appended));
        return;
    }
    case MakeAssign::Shell:
        makefile.diagnostics.push_back(base::makeNote(
            makeSite(line).location,
            std::format("'{} != ...' runs a shell command, which is not done here", name)));
        makefile.variables.set(name, MakeVariable{.origin = origin, .certain = false});
        return;
    }
}

void Reader::startDefinition(std::string_view rest, bool isOverride, u32 line) {
    const auto [name, operation] = splitFirstWord(rest);
    MakeAssign kind = MakeAssign::Recursive;
    if (operation == ":=" || operation == "::=") {
        kind = MakeAssign::Simple;
    } else if (operation == "+=") {
        kind = MakeAssign::Append;
    } else if (operation == "?=") {
        kind = MakeAssign::Conditional;
    }
    definition = Definition{.name = std::string(name),
                            .assign = kind,
                            .isOverride = isOverride,
                            .active = isActive(),
                            .line = line};
}

void Reader::finishDefinition(const Definition& finished) {
    if (!finished.active) {
        return;
    }
    std::string body;
    for (usize at = 0; at < finished.body.size(); ++at) {
        if (at > 0) {
            body += '\n';
        }
        body += finished.body[at];
    }
    assign(finished.name, finished.assign, body, finished.isOverride, finished.line);
}

} // namespace

std::expected<Makefile, base::Diagnostic> readMakefile(const std::filesystem::path& file,
                                                       const MakeOverrides& overrides) {
    std::error_code failure;
    if (!std::filesystem::exists(file, failure)) {
        return std::unexpected(
            base::makeError(base::SourceLocation{.file = file}, "cannot find the Makefile"));
    }

    Makefile makefile;
    for (const auto& [name, value] : overrides) {
        makefile.variables.set(
            name,
            MakeVariable{.value = value, .recursive = false, .origin = MakeOrigin::CommandLine});
    }
    Reader(file.parent_path(), makefile).readFile(file, 0);
    return makefile;
}

} // namespace makeparser
