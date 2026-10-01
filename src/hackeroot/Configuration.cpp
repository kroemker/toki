#include "Configuration.h"

#include "z64/MacroTable.h"
#include "z64/Predefines.h"
#include "z64/Preprocessor.h"
#include "z64/Scrape.h"
#include "z64/SourceFile.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hackeroot {

Configuration readConfiguration(const z64::MacroTable& scraped,
                                const std::filesystem::path& repository,
                                std::span<const std::string> defineArguments) {
    const std::filesystem::path include = repository / "include";
    const z64::PreprocessOptions options{
        .searchPaths = {include, repository},
        .allowedRoots = {include / "config.h", include / "versions.h", include / "config",
                         include / "libc" / "stdbool.h"},
    };

    const z64::MacroTable nothing;
    std::string source = z64::predefineSource(defineArguments);
    source += "#include \"config.h\"\n";
    z64::TranslationUnit unit = z64::preprocessText(nothing, repository / "<build configuration>",
                                                    std::move(source), options);

    Configuration configuration;
    configuration.macros = z64::MacroTable(&scraped);
    for (const std::string_view name : unit.macros.definedNames()) {
        configuration.macros.define(*unit.macros.find(name));
    }
    for (const z64::SourceFile* file : unit.sources.getFiles()) {
        for (const std::string_view name : z64::mentionedNames(*file)) {
            if (!unit.macros.contains(name)) {
                configuration.macros.remove(name);
            }
        }
    }
    configuration.sources = std::move(unit.sources);
    configuration.diagnostics = std::move(unit.diagnostics);
    return configuration;
}

} // namespace hackeroot
