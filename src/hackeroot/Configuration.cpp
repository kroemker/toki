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

namespace {

constexpr std::string_view CompilerPredefines = "#define _LANGUAGE_C 1\n"
                                                "#define __mips__ 1\n"
                                                "#define _MIPS_SZINT 32\n"
                                                "#define _MIPS_SZLONG 32\n"
                                                "#define _MIPS_SZPTR 32\n"
                                                "#define __mips 3\n"
                                                "#define _MIPSEB 1\n"
                                                "#define __MIPSEB__ 1\n"
                                                "#define __GNUC__ 10\n"
                                                "#define __GNUC_MINOR__ 0\n"
                                                "#define __STDC__ 1\n"
                                                "#define __STDC_VERSION__ 201112L\n";

} // namespace

cparser::PreprocessOptions makeConfigurationOptions(const std::filesystem::path& repository) {
    const std::filesystem::path include = repository / "include";
    return cparser::PreprocessOptions{
        .searchPaths = {include, repository},
        .allowedRoots = {include / "config.h", include / "versions.h", include / "config",
                         include / "libc" / "stdbool.h"},
        .dialect = &z64::getDecompDialect(),
    };
}

std::string makeConfigurationSource(const std::span<const std::string> defineArguments) {
    std::string source(CompilerPredefines);
    source += cparser::buildPredefineSource(defineArguments);
    source += "#include \"config.h\"\n";
    return source;
}

Configuration readConfiguration(const cparser::MacroTable& scraped,
                                const std::filesystem::path& repository,
                                std::span<const std::string> defineArguments) {
    const cparser::MacroTable nothing;
    cparser::TranslationUnit unit = cparser::preprocessText(
        nothing, repository / "<build configuration>", makeConfigurationSource(defineArguments),
        makeConfigurationOptions(repository));

    Configuration configuration;
    configuration.macros = cparser::MacroTable(&scraped);
    for (const std::string_view name : unit.macros.getDefinedNames()) {
        configuration.macros.define(*unit.macros.find(name));
    }
    for (const cparser::SourceFile* file : unit.sources.getFiles()) {
        for (const std::string_view name : cparser::findMentionedNames(*file)) {
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
