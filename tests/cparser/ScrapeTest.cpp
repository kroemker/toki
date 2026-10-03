#include "cparser/Scrape.h"

#include "cparser/MacroTable.h"
#include "cparser/SourceFile.h"
#include "cparser/Token.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path getFixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "cparser/fixtures";
}

} // namespace

TEST_CASE("a header tree gives up its defines without being preprocessed", "[cparser][scrape]") {
    const cparser::ScrapeResult scraped = cparser::scrapeDefinitions(getFixtures() / "headers");
    const cparser::MacroTable& macros = scraped.macros;

    REQUIRE(scraped.headers == 2);
    REQUIRE(scraped.diagnostics.empty());
    REQUIRE(macros.contains("ONLY_IN_A"));
    REQUIRE(macros.contains("ONLY_IN_B"));

    SECTION("a function-like macro keeps its parameters") {
        const cparser::Macro* macro = macros.find("ONLY_IN_A");
        REQUIRE(macro != nullptr);
        REQUIRE(macro->functionLike);
        REQUIRE(macro->parameters == std::vector<std::string>{"x"});
    }

    SECTION("every body views into a header the result owns") {
        const cparser::Macro* shared = macros.find("SHARED_THING");
        REQUIRE(shared != nullptr);
        const cparser::SourceFile* file = shared->body.front().file;
        REQUIRE(file != nullptr);
        REQUIRE(scraped.sources.find(file->path) == file);
    }

    SECTION("only headers are read") {
        REQUIRE_FALSE(macros.contains("NEVER_SCRAPED"));
    }

    SECTION("both arms of an unevaluated conditional are seen, and the first name wins") {
        const cparser::Macro* shared = macros.find("SHARED_THING");
        REQUIRE(shared != nullptr);
        REQUIRE(shared->body.size() == 1);
        REQUIRE(shared->body.front().text == "1");
    }
}

TEST_CASE("a directory that is not there is an error, not a crash", "[cparser][scrape]") {
    const cparser::ScrapeResult scraped = cparser::scrapeDefinitions(getFixtures() / "nowhere");

    REQUIRE(scraped.headers == 0);
    REQUIRE(scraped.diagnostics.size() == 1);
}

TEST_CASE("every name a file defines or undefines is found, whatever the conditionals say",
          "[cparser][scrape]") {
    const cparser::SourceFile file{
        .path = "memory.h",
        .contents = "#if 0\n#define HIDDEN 1\n#else\n#undef SHOWN\n#endif\n#define LAST(x) x\n"};

    REQUIRE(cparser::findMentionedNames(file) ==
            std::vector<std::string_view>{"HIDDEN", "SHOWN", "LAST"});
}

TEST_CASE("a path that is not a directory is reported, never thrown", "[cparser][scrape]") {
    const cparser::ScrapeResult scraped =
        cparser::scrapeDefinitions(getFixtures() / "headers/a_first.h");

    REQUIRE(scraped.headers == 0);
    REQUIRE(scraped.diagnostics.size() == 1);
}
