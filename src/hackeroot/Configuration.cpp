#include "Configuration.h"

#include "cparser/MacroTable.h"
#include "cparser/Predefines.h"
#include "cparser/Preprocessor.h"
#include "cparser/Scrape.h"
#include "cparser/SourceFile.h"
#include "z64/DecompDialect.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hackeroot {

Configuration readConfiguration(const cparser::MacroTable& scraped,
                                const std::filesystem::path& repository,
                                std::span<const std::string> defineArguments) {
    const std::filesystem::path include = repository / "include";
    const cparser::PreprocessOptions options{
        .searchPaths = {include, repository},
        .allowedRoots = {include / "config.h", include / "versions.h", include / "config",
                         include / "libc" / "stdbool.h"},
        .dialect = &z64::decompDialect(),
    };

    const cparser::MacroTable nothing;
    std::string source = cparser::predefineSource(defineArguments);
    source += "#include \"config.h\"\n";
    cparser::TranslationUnit unit = cparser::preprocessText(
        nothing, repository / "<build configuration>", std::move(source), options);

    Configuration configuration;
    configuration.macros = cparser::MacroTable(&scraped);
    for (const std::string_view name : unit.macros.definedNames()) {
        configuration.macros.define(*unit.macros.find(name));
    }
    for (const cparser::SourceFile* file : unit.sources.getFiles()) {
        for (const std::string_view name : cparser::mentionedNames(*file)) {
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
