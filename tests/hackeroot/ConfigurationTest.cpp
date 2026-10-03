#include "hackeroot/Configuration.h"

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Scrape.h"
#include "cparser/TableReader.h"
#include "cparser/Token.h"
#include "hackeroot/BuildDefines.h"
#include "makeparser/Makefile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::array SceneMacros = {std::string_view("DEFINE_SCENE")};

[[nodiscard]] std::filesystem::path checkout() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

[[nodiscard]] std::vector<std::string> definesFor(const makeparser::MakeOverrides& settings) {
    std::expected<hackeroot::BuildDefines, base::Diagnostic> defines =
        hackeroot::readBuildDefines(checkout(), settings);
    REQUIRE(defines.has_value());
    return std::move(defines->arguments);
}

/// The scrape and the configuration over it, built in place because the
/// configuration's macros point at the scrape's.
class Harness {
public:
    explicit Harness(const makeparser::MakeOverrides& settings = {})
        : scraped(cparser::scrapeDefinitions(checkout() / "include")),
          configuration(
              hackeroot::readConfiguration(scraped.macros, checkout(), definesFor(settings))) {}

    [[nodiscard]] std::string bodyOf(std::string_view name) const {
        const cparser::Macro* macro = configuration.macros.find(name);
        REQUIRE(macro != nullptr);
        std::string body;
        for (const cparser::Token& token : macro->body) {
            body += token.text;
        }
        return body;
    }

    [[nodiscard]] std::vector<std::string> scenes() const {
        std::expected<cparser::TableResult, base::Diagnostic> table = cparser::readTable(
            configuration.macros, checkout() / "include/tables/scene_table.h", SceneMacros, {});
        REQUIRE(table.has_value());
        REQUIRE_FALSE(
            std::ranges::any_of(table->diagnostics, [](const base::Diagnostic& diagnostic) {
                return diagnostic.severity != base::Severity::Note;
            }));

        std::vector<std::string> names;
        for (const cparser::TableRow& row : table->rows) {
            names.emplace_back(row.arguments.front());
        }
        return names;
    }

    cparser::ScrapeResult scraped;
    hackeroot::Configuration configuration;
};

} // namespace

TEST_CASE("the build's defines and the config headers are read together",
          "[hackeroot][configuration]") {
    const Harness harness;

    REQUIRE(harness.bodyOf("DEBUG_FEATURES") == "1");
    REQUIRE(harness.bodyOf("OOT_VERSION") == "NTSC_1_2");
    REQUIRE(harness.bodyOf("DEBUG_ASSETS") == "0");
    REQUIRE(harness.bodyOf("INCLUDE_EXAMPLE_SCENE") == "true");
}

TEST_CASE("the configured arm of a conditional wins over the scrape's first one",
          "[hackeroot][configuration]") {
    const Harness harness({{"DEBUG_FEATURES", "0"}});

    REQUIRE(harness.bodyOf("INCLUDE_EXAMPLE_SCENE") == "false");
    REQUIRE(harness.scraped.macros.find("INCLUDE_EXAMPLE_SCENE")->body.front().text == "true");
}

TEST_CASE("a name the configuration defines only under other settings is hidden",
          "[hackeroot][configuration]") {
    const Harness debug;
    REQUIRE(debug.scraped.macros.contains("ONLY_WITHOUT_DEBUG"));
    REQUIRE_FALSE(debug.configuration.macros.contains("ONLY_WITHOUT_DEBUG"));

    const Harness release({{"DEBUG_FEATURES", "0"}});
    REQUIRE(release.configuration.macros.contains("ONLY_WITHOUT_DEBUG"));
}

TEST_CASE("names the configuration never mentions still come from the scrape",
          "[hackeroot][configuration]") {
    const Harness harness;

    REQUIRE(harness.bodyOf("ONLY_IN_HEADERS") == "7");
    REQUIRE(harness.configuration.macros.count() > 0);
}

TEST_CASE("a scene table's ordinals follow the build's settings", "[hackeroot][configuration]") {
    REQUIRE(Harness().scenes() ==
            std::vector<std::string>{"first_scene", "example_scene", "custom_scene"});
    REQUIRE(Harness({{"DEBUG_FEATURES", "0"}}).scenes() ==
            std::vector<std::string>{"first_scene", "custom_scene"});
    REQUIRE(Harness({{"VERSION", "gc-eu-mq-dbg"}}).scenes() ==
            std::vector<std::string>{"first_scene", "test_scene", "example_scene", "custom_scene"});
}

TEST_CASE("reading the configuration leaves the scrape untouched", "[hackeroot][configuration]") {
    const Harness harness({{"DEBUG_FEATURES", "0"}});

    REQUIRE_FALSE(harness.scraped.macros.contains("DEBUG_FEATURES"));
    REQUIRE(harness.scraped.macros.contains("CONFIG_H"));
}
