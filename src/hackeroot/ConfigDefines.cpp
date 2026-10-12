#include "ConfigDefines.h"

#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/SourceFile.h"
#include "cparser/Token.h"
#include "hackeroot/Configuration.h"
#include "hackeroot/Types.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::string_view Safeguards = "config_safeguards.h";

struct Definition {
    std::string name;
    const cparser::SourceFile* file = nullptr;
    usize line = 0;
};

[[nodiscard]] std::string_view trimSpaces(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] std::vector<std::string_view> splitLines(const std::string_view text) {
    std::vector<std::string_view> lines;
    usize begin = 0;
    while (begin <= text.size()) {
        const usize end = std::min(text.find('\n', begin), text.size());
        lines.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

[[nodiscard]] std::string readWrittenValue(const std::string_view line,
                                           const std::string_view name) {
    std::string_view rest = trimSpaces(line);
    rest.remove_prefix(std::min(rest.find(name) + name.size(), rest.size()));
    const usize comment = std::min(rest.find("//"), rest.find("/*"));
    return std::string(
        trimSpaces(comment == std::string_view::npos ? rest : rest.substr(0, comment)));
}

[[nodiscard]] std::string_view stripCommentMarks(std::string_view line) {
    line = trimSpaces(line);
    for (const std::string_view mark : {"/**", "/*", "*/", "//", "*"}) {
        if (line.starts_with(mark)) {
            line.remove_prefix(mark.size());
            break;
        }
    }
    if (line.ends_with("*/")) {
        line.remove_suffix(2);
    }
    return trimSpaces(line);
}

[[nodiscard]] std::string readComment(const std::vector<std::string_view>& lines,
                                      const usize line) {
    if (line < 2) {
        return {};
    }
    usize first = line - 1;
    const std::string_view above = trimSpaces(lines[first - 1]);
    if (above.ends_with("*/")) {
        while (first > 1 && !trimSpaces(lines[first - 1]).starts_with("/*")) {
            --first;
        }
    } else if (above.starts_with("//")) {
        while (first > 1 && trimSpaces(lines[first - 2]).starts_with("//")) {
            --first;
        }
    } else {
        return {};
    }
    std::string comment;
    for (usize at = first; at < line; ++at) {
        const std::string_view words = stripCommentMarks(lines[at - 1]);
        if (words.empty()) {
            continue;
        }
        comment += comment.empty() ? "" : " ";
        comment += words;
    }
    return comment;
}

[[nodiscard]] std::string spell(const cparser::Macro& macro) {
    std::string spelled;
    for (const cparser::Token& token : macro.body) {
        spelled += spelled.empty() ? "" : " ";
        spelled += token.text;
    }
    return spelled;
}

[[nodiscard]] bool isSetting(const cparser::SourceFile& file,
                             const std::filesystem::path& configFolder) {
    const std::filesystem::path path = std::filesystem::path(file.path).lexically_normal();
    return path.parent_path() == configFolder && path.filename() != Safeguards;
}

} // namespace

std::vector<DocumentedDefine> listDocumentedDefines(const std::string_view text,
                                                    const std::string_view prefix) {
    const std::vector<std::string_view> lines = splitLines(text);
    std::vector<DocumentedDefine> defines;
    for (usize at = 0; at < lines.size(); ++at) {
        std::string_view line = trimSpaces(lines[at]);
        if (!line.starts_with("#define ")) {
            continue;
        }
        line = trimSpaces(line.substr(std::string_view("#define ").size()));
        const usize end = line.find_first_of(" \t(");
        const std::string_view name = line.substr(0, end);
        const bool isFunctionLike = end != std::string_view::npos && line[end] == '(';
        if (!name.starts_with(prefix) || isFunctionLike ||
            std::ranges::contains(defines, name, &DocumentedDefine::name)) {
            continue;
        }
        defines.push_back(
            DocumentedDefine{.name = std::string(name), .comment = readComment(lines, at + 1)});
    }
    return defines;
}

std::vector<ConfigDefine> readConfigDefines(const std::filesystem::path& repository,
                                            const std::span<const std::string> defineArguments) {
    const std::filesystem::path configFolder =
        (repository / "include" / "config").lexically_normal();
    std::vector<Definition> definitions;
    cparser::PreprocessOptions options = makeConfigurationOptions(repository);
    options.onDefine = [&](const cparser::Macro& macro, const cparser::Token& directive) {
        if (macro.functionLike || macro.body.empty() || directive.file == nullptr ||
            !isSetting(*directive.file, configFolder)) {
            return;
        }
        std::erase_if(definitions, [&](const Definition& known) {
            return known.name == macro.name && known.file == directive.file;
        });
        definitions.push_back(
            Definition{.name = macro.name, .file = directive.file, .line = directive.line});
    };
    const cparser::MacroTable nothing;
    const cparser::TranslationUnit unit =
        cparser::preprocessText(nothing, repository / "<build configuration>",
                                makeConfigurationSource(defineArguments), options);

    std::vector<const cparser::SourceFile*> files;
    for (const Definition& definition : definitions) {
        if (!std::ranges::contains(files, definition.file)) {
            files.push_back(definition.file);
        }
    }
    std::ranges::stable_sort(definitions, {}, [&files](const Definition& definition) {
        return std::pair(std::ranges::find(files, definition.file) - files.begin(),
                         definition.line);
    });

    std::vector<ConfigDefine> defines;
    for (const Definition& definition : definitions) {
        const std::vector<std::string_view> lines = splitLines(definition.file->contents);
        if (definition.line == 0 || definition.line > lines.size()) {
            continue;
        }
        const cparser::Macro* effective = unit.macros.find(definition.name);
        defines.push_back(ConfigDefine{
            .name = definition.name,
            .file = std::filesystem::path(definition.file->path)
                        .lexically_normal()
                        .lexically_relative(repository.lexically_normal()),
            .line = definition.line,
            .written = readWrittenValue(lines[definition.line - 1], definition.name),
            .comment = readComment(lines, definition.line),
            .effective = effective != nullptr ? std::optional(spell(*effective)) : std::nullopt,
        });
    }
    return defines;
}

} // namespace hackeroot
