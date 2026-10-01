#include "BuildDefines.h"

#include "hackeroot/MakeVariables.h"
#include "hackeroot/MakeWords.h"
#include "hackeroot/Makefile.h"
#include "hackeroot/ShellWords.h"
#include "z64/Diagnostic.h"

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

[[nodiscard]] char upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] std::string versionMacro(std::string_view version) {
    std::string macro;
    for (const char c : version) {
        macro += c == '-' || c == '.' ? '_' : upper(c);
    }
    return macro;
}

[[nodiscard]] std::string uppercase(std::string_view text) {
    std::string result;
    for (const char c : text) {
        result += upper(c);
    }
    return result;
}

[[nodiscard]] bool isDefineArgument(std::string_view argument) {
    return argument.starts_with("-D") || argument.starts_with("-U") ||
           argument.starts_with("-std=");
}

[[nodiscard]] MakeOverrides deriveOverrides(const Makefile& first, const MakeOverrides& settings) {
    MakeOverrides overrides = settings;
    std::vector<z64::Diagnostic> discarded;
    const auto valueOf = [&](std::string_view name) {
        return stripWords(
            first.variables.expand(std::format("$({})", name), MakeSite{}, discarded).text);
    };
    if (!overrides.contains("VERSION_MACRO")) {
        overrides.emplace("VERSION_MACRO", versionMacro(valueOf("VERSION")));
    }
    if (!overrides.contains("COMPRESSION_TYPE")) {
        overrides.emplace("COMPRESSION_TYPE", uppercase(valueOf("COMPRESSION")));
    }
    return overrides;
}

} // namespace

std::expected<BuildDefines, z64::Diagnostic>
readBuildDefines(const std::filesystem::path& repository, const MakeOverrides& settings) {
    const std::filesystem::path path = repository / "Makefile";
    const std::expected<Makefile, z64::Diagnostic> first = readMakefile(path, settings);
    if (!first) {
        return std::unexpected(first.error());
    }
    std::expected<Makefile, z64::Diagnostic> makefile =
        readMakefile(path, deriveOverrides(*first, settings));
    if (!makefile) {
        return std::unexpected(makefile.error());
    }

    BuildDefines defines;
    defines.diagnostics = std::move(makefile->diagnostics);
    const MakeSite site{.location = z64::SourceLocation{.file = path}};

    if (makefile->variables.find("CPP_DEFINES") == nullptr) {
        defines.diagnostics.push_back(
            z64::warning(site.location, "the Makefile leaves CPP_DEFINES undefined"));
    }
    for (const std::string_view variable : DefineVariables) {
        for (const MakeText& word :
             makefile->variables.expandWords(variable, site, defines.diagnostics)) {
            for (std::string& argument : splitShellWords(word.text)) {
                if (!isDefineArgument(argument)) {
                    continue;
                }
                if (!word.certain) {
                    defines.diagnostics.push_back(z64::note(
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
