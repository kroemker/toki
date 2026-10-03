#include "base/Diagnostic.h"
#include "base/Types.h"
#include "cparser/Expression.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/Scrape.h"
#include "cparser/TableReader.h"
#include "cparser/Token.h"
#include "hackeroot/BuildDefines.h"
#include "hackeroot/Configuration.h"
#include "z64/DecompDialect.h"
#include "z64/TextureIncludes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::array SceneMacros = {std::string_view("DEFINE_SCENE")};
constexpr std::array ActorMacros = {std::string_view("DEFINE_ACTOR"),
                                    std::string_view("DEFINE_ACTOR_INTERNAL"),
                                    std::string_view("DEFINE_ACTOR_UNSET")};
constexpr std::array ObjectMacros = {std::string_view("DEFINE_OBJECT"),
                                     std::string_view("DEFINE_OBJECT_UNSET")};
constexpr std::array EntranceMacros = {std::string_view("DEFINE_ENTRANCE")};

[[nodiscard]] std::vector<std::string>
collectLoudest(const std::vector<base::Diagnostic>& diagnostics) {
    std::vector<std::string> described;
    for (const base::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != base::Severity::Note) {
            described.push_back(base::describe(diagnostic));
        }
    }
    return described;
}

void requireTable(const cparser::MacroTable& macros, const std::filesystem::path& file,
                  std::span<const std::string_view> entryMacros) {
    INFO(file.string());
    const std::expected<cparser::TableResult, base::Diagnostic> table =
        cparser::readTable(macros, file, entryMacros, {});
    REQUIRE(table.has_value());
    REQUIRE_FALSE(table->rows.empty());
    REQUIRE(collectLoudest(table->diagnostics).empty());
}

[[nodiscard]] std::filesystem::path findExtractedScene(const std::filesystem::path& repository,
                                                       const std::filesystem::path& scene) {
    std::error_code failure;
    for (std::filesystem::directory_iterator entry(repository / "extracted", failure);
         !failure && entry != std::filesystem::directory_iterator(); entry.increment(failure)) {
        if (std::filesystem::exists(entry->path() / scene, failure)) {
            return entry->path();
        }
    }
    return {};
}

void requireImagesLinked(const cparser::MacroTable& macros,
                         const std::filesystem::path& repository) {
    const std::filesystem::path scene = "assets/scenes/overworld/spot00/spot00_scene.c";
    const std::filesystem::path extracted = findExtractedScene(repository, scene);
    INFO("no extracted spot00 under " << (repository / "extracted").string());
    REQUIRE_FALSE(extracted.empty());

    const std::expected<cparser::TranslationUnit, base::Diagnostic> unit = cparser::preprocess(
        macros, extracted / scene,
        cparser::PreprocessOptions{.searchPaths = {repository, extracted},
                                   .allowedRoots = {repository / "assets", extracted / "assets"},
                                   .resolveMissingInclude = z64::resolveTextureInclude,
                                   .dialect = &z64::getDecompDialect()});
    REQUIRE(unit.has_value());
    REQUIRE_FALSE(unit->embedded.empty());

    const cparser::ParseResult parsed = cparser::parseDeclarations(
        *unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});
    REQUIRE(collectLoudest(unit->diagnostics).empty());
    REQUIRE(collectLoudest(parsed.diagnostics).empty());
    std::vector<base::usize> uses(unit->embedded.size(), 0);
    for (const cparser::Declaration& declaration : parsed.declarations) {
        bool fromImage = false;
        for (const cparser::Token& token : declaration.initializer) {
            if (token.kind == cparser::TokenKind::Embedded) {
                REQUIRE(token.embedded < uses.size());
                ++uses[token.embedded];
                fromImage = true;
            }
        }
        if (fromImage) {
            INFO(declaration.name);
            REQUIRE(declaration.arrayLength.value_or(0) > 0);
        }
    }
    REQUIRE(std::ranges::all_of(uses, [](base::usize count) { return count == 1; }));
}

} // namespace

TEST_CASE("a real checkout reads with its build's configuration and without warnings",
          "[hackeroot][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }

    const std::expected<hackeroot::BuildDefines, base::Diagnostic> defines =
        hackeroot::readBuildDefines(repository, {});
    REQUIRE(defines.has_value());
    REQUIRE(collectLoudest(defines->diagnostics).empty());
    for (const std::string_view wanted :
         {"-DDEBUG_FEATURES=", "-DOOT_VERSION=", "-DPLATFORM_N64="}) {
        INFO(wanted);
        REQUIRE(std::ranges::any_of(defines->arguments, [wanted](const std::string& argument) {
            return argument.starts_with(wanted);
        }));
    }

    const cparser::ScrapeResult scraped = cparser::scrapeDefinitions(repository / "include");
    const hackeroot::Configuration configuration =
        hackeroot::readConfiguration(scraped.macros, repository, defines->arguments);
    REQUIRE(collectLoudest(configuration.diagnostics).empty());
    REQUIRE(configuration.macros.contains("DEBUG_FEATURES"));

    const std::filesystem::path tables = repository / "include/tables";
    requireTable(configuration.macros, tables / "scene_table.h", SceneMacros);
    requireTable(configuration.macros, tables / "actor_table.h", ActorMacros);
    requireTable(configuration.macros, tables / "object_table.h", ObjectMacros);
    requireTable(configuration.macros, tables / "entrance_table.h", EntranceMacros);
    requireImagesLinked(configuration.macros, repository);
}
