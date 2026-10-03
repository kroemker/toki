#include "hackeroot/BuildDefines.h"
#include "hackeroot/Configuration.h"
#include "z64/Diagnostic.h"
#include "z64/Expression.h"
#include "z64/Parser.h"
#include "z64/Preprocessor.h"
#include "z64/Scrape.h"
#include "z64/TableReader.h"
#include "z64/Token.h"
#include "z64/Types.h"

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

[[nodiscard]] std::vector<std::string> loudest(const std::vector<z64::Diagnostic>& diagnostics) {
    std::vector<std::string> described;
    for (const z64::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != z64::Severity::Note) {
            described.push_back(z64::describe(diagnostic));
        }
    }
    return described;
}

void requireTable(const z64::MacroTable& macros, const std::filesystem::path& file,
                  std::span<const std::string_view> entryMacros) {
    INFO(file.string());
    const std::expected<z64::TableResult, z64::Diagnostic> table =
        z64::readTable(macros, file, entryMacros, {});
    REQUIRE(table.has_value());
    REQUIRE_FALSE(table->rows.empty());
    REQUIRE(loudest(table->diagnostics).empty());
}

[[nodiscard]] std::filesystem::path extractedScene(const std::filesystem::path& repository,
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

void requireImagesLinked(const z64::MacroTable& macros, const std::filesystem::path& repository) {
    const std::filesystem::path scene = "assets/scenes/overworld/spot00/spot00_scene.c";
    const std::filesystem::path extracted = extractedScene(repository, scene);
    INFO("no extracted spot00 under " << (repository / "extracted").string());
    REQUIRE_FALSE(extracted.empty());

    const std::expected<z64::TranslationUnit, z64::Diagnostic> unit = z64::preprocess(
        macros, extracted / scene,
        z64::PreprocessOptions{.searchPaths = {repository, extracted},
                               .allowedRoots = {repository / "assets", extracted / "assets"}});
    REQUIRE(unit.has_value());
    REQUIRE_FALSE(unit->images.empty());

    const z64::ParseResult parsed = z64::parseDeclarations(*unit, z64::EvalContext{});
    REQUIRE(loudest(unit->diagnostics).empty());
    REQUIRE(loudest(parsed.diagnostics).empty());
    std::vector<z64::usize> uses(unit->images.size(), 0);
    for (const z64::Declaration& declaration : parsed.declarations) {
        bool fromImage = false;
        for (const z64::Token& token : declaration.initializer) {
            if (token.kind == z64::TokenKind::Image) {
                REQUIRE(token.image < uses.size());
                ++uses[token.image];
                fromImage = true;
            }
        }
        if (fromImage) {
            INFO(declaration.name);
            REQUIRE(declaration.arrayLength.value_or(0) > 0);
        }
    }
    REQUIRE(std::ranges::all_of(uses, [](z64::usize count) { return count == 1; }));
}

} // namespace

TEST_CASE("a real checkout reads with its build's configuration and without warnings",
          "[hackeroot][checkout]") {
    const std::filesystem::path repository(TOKI_HACKEROOT);
    if (repository.empty()) {
        SKIP("configure with -DTOKI_HACKEROOT=<path to a HackerOoT checkout> to run this");
    }

    const std::expected<hackeroot::BuildDefines, z64::Diagnostic> defines =
        hackeroot::readBuildDefines(repository, {});
    REQUIRE(defines.has_value());
    REQUIRE(loudest(defines->diagnostics).empty());
    for (const std::string_view wanted :
         {"-DDEBUG_FEATURES=", "-DOOT_VERSION=", "-DPLATFORM_N64="}) {
        INFO(wanted);
        REQUIRE(std::ranges::any_of(defines->arguments, [wanted](const std::string& argument) {
            return argument.starts_with(wanted);
        }));
    }

    const z64::ScrapeResult scraped = z64::scrapeDefinitions(repository / "include");
    const hackeroot::Configuration configuration =
        hackeroot::readConfiguration(scraped.macros, repository, defines->arguments);
    REQUIRE(loudest(configuration.diagnostics).empty());
    REQUIRE(configuration.macros.contains("DEBUG_FEATURES"));

    const std::filesystem::path tables = repository / "include/tables";
    requireTable(configuration.macros, tables / "scene_table.h", SceneMacros);
    requireTable(configuration.macros, tables / "actor_table.h", ActorMacros);
    requireTable(configuration.macros, tables / "object_table.h", ObjectMacros);
    requireTable(configuration.macros, tables / "entrance_table.h", EntranceMacros);
    requireImagesLinked(configuration.macros, repository);
}
