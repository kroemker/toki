#include "BuildDefines.h"

#include "base/Diagnostic.h"
#include "makeparser/MakeVariables.h"
#include "makeparser/MakeWords.h"
#include "makeparser/Makefile.h"
#include "makeparser/ShellWords.h"

#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr std::array DefineVariables = {std::string_view("CPP_DEFINES"),
                                        std::string_view("GBI_DEFINES")};

[[nodiscard]] char capitalize(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] std::string makeVersionMacro(std::string_view version) {
    std::string macro;
    for (const char c : version) {
        macro += c == '-' || c == '.' ? '_' : capitalize(c);
    }
    return macro;
}

[[nodiscard]] std::string capitalizeAll(std::string_view text) {
    std::string result;
    for (const char c : text) {
        result += capitalize(c);
    }
    return result;
}

[[nodiscard]] bool isDefineArgument(std::string_view argument) {
    return argument.starts_with("-D") || argument.starts_with("-U") ||
           argument.starts_with("-std=");
}

[[nodiscard]] makeparser::MakeOverrides deriveOverrides(const makeparser::Makefile& first,
                                                        const makeparser::MakeOverrides& settings) {
    makeparser::MakeOverrides overrides = settings;
    std::vector<base::Diagnostic> discarded;
    const auto valueOf = [&](std::string_view name) {
        return makeparser::stripWords(
            first.variables.expand(std::format("$({})", name), makeparser::MakeSite{}, discarded)
                .text);
    };
    if (!overrides.contains("VERSION_MACRO")) {
        overrides.emplace("VERSION_MACRO", makeVersionMacro(valueOf("VERSION")));
    }
    if (!overrides.contains("COMPRESSION_TYPE")) {
        overrides.emplace("COMPRESSION_TYPE", capitalizeAll(valueOf("COMPRESSION")));
    }
    return overrides;
}

} // namespace

std::expected<BuildDefines, base::Diagnostic>
readBuildDefines(const std::filesystem::path& repository,
                 const makeparser::MakeOverrides& settings) {
    const std::filesystem::path path = repository / "Makefile";
    const std::expected<makeparser::Makefile, base::Diagnostic> first =
        makeparser::readMakefile(path, settings);
    if (!first) {
        return std::unexpected(first.error());
    }
    std::expected<makeparser::Makefile, base::Diagnostic> makefile =
        makeparser::readMakefile(path, deriveOverrides(*first, settings));
    if (!makefile) {
        return std::unexpected(makefile.error());
    }

    BuildDefines defines;
    defines.diagnostics = std::move(makefile->diagnostics);
    const makeparser::MakeSite site{.location = base::SourceLocation{.file = path}};
    defines.version = makeparser::stripWords(
        makefile->variables.expand("$(VERSION)", site, defines.diagnostics).text);
    const std::string mapFile = makeparser::stripWords(
        makefile->variables.expand("$(MAP)", site, defines.diagnostics).text);
    if (!mapFile.empty()) {
        defines.mapFile = repository / mapFile;
    }

    if (makefile->variables.find("CPP_DEFINES") == nullptr) {
        defines.diagnostics.push_back(
            base::makeWarning(site.location, "the Makefile leaves CPP_DEFINES undefined"));
    }
    for (const std::string_view variable : DefineVariables) {
        for (const makeparser::MakeText& word :
             makefile->variables.expandWords(variable, site, defines.diagnostics)) {
            for (std::string& argument : makeparser::splitShellWords(word.text)) {
                if (!isDefineArgument(argument)) {
                    continue;
                }
                if (!word.certain) {
                    defines.diagnostics.push_back(base::makeNote(
                        site.location,
                        std::format("{} depends on something make would run, and is read as '{}'",
                                    variable, argument)));
                }
                defines.arguments.push_back(std::move(argument));
            }
        }
    }
    return defines;
}

} // namespace hackeroot
