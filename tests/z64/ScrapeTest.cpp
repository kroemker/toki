#include "z64/Scrape.h"

#include "z64/MacroTable.h"
#include "z64/SourceFile.h"
#include "z64/Token.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "z64/fixtures";
}

} // namespace

TEST_CASE("a header tree gives up its defines without being preprocessed", "[z64][scrape]") {
    const z64::ScrapeResult scraped = z64::scrapeDefinitions(fixtures() / "headers");
    const z64::MacroTable& macros = scraped.macros;

    REQUIRE(scraped.headers == 2);
    REQUIRE(scraped.diagnostics.empty());
    REQUIRE(macros.contains("ONLY_IN_A"));
    REQUIRE(macros.contains("ONLY_IN_B"));

    SECTION("a function-like macro keeps its parameters") {
        const z64::Macro* macro = macros.find("ONLY_IN_A");
        REQUIRE(macro != nullptr);
        REQUIRE(macro->functionLike);
        REQUIRE(macro->parameters == std::vector<std::string>{"x"});
    }

    SECTION("every body views into a header the result owns") {
        const z64::Macro* shared = macros.find("SHARED_THING");
        REQUIRE(shared != nullptr);
        const z64::SourceFile* file = shared->body.front().file;
        REQUIRE(file != nullptr);
        REQUIRE(scraped.sources.find(file->path) == file);
    }

    SECTION("only headers are read") {
        REQUIRE_FALSE(macros.contains("NEVER_SCRAPED"));
    }

    SECTION("both arms of an unevaluated conditional are seen, and the first name wins") {
        const z64::Macro* shared = macros.find("SHARED_THING");
        REQUIRE(shared != nullptr);
        REQUIRE(shared->body.size() == 1);
        REQUIRE(shared->body.front().text == "1");
    }
}

TEST_CASE("a directory that is not there is an error, not a crash", "[z64][scrape]") {
    const z64::ScrapeResult scraped = z64::scrapeDefinitions(fixtures() / "nowhere");

    REQUIRE(scraped.headers == 0);
    REQUIRE(scraped.diagnostics.size() == 1);
}

TEST_CASE("every name a file defines or undefines is found, whatever the conditionals say",
          "[z64][scrape]") {
    const z64::SourceFile file{
        .path = "memory.h",
        .contents = "#if 0\n#define HIDDEN 1\n#else\n#undef SHOWN\n#endif\n#define LAST(x) x\n"};

    REQUIRE(z64::mentionedNames(file) == std::vector<std::string_view>{"HIDDEN", "SHOWN", "LAST"});
}
