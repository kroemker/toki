#include "cparser/TableReader.h"

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/Types.h"
#include "cparser/Value.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::array SceneMacros = {std::string_view("DEFINE_SCENE")};
constexpr std::array ObjectMacros = {std::string_view("DEFINE_OBJECT"),
                                     std::string_view("DEFINE_OBJECT_UNSET")};

[[nodiscard]] std::filesystem::path getFixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "cparser/fixtures";
}

[[nodiscard]] bool reports(const cparser::TableResult& table, std::string_view text) {
    return std::ranges::any_of(table.diagnostics, [text](const base::Diagnostic& diagnostic) {
        return diagnostic.message.contains(text);
    });
}

class Harness {
public:
    [[nodiscard]] cparser::TableResult read(const std::filesystem::path& file,
                                            std::span<const std::string_view> entryMacros,
                                            const cparser::PreprocessOptions& options = {}) const {
        std::expected<cparser::TableResult, base::Diagnostic> table =
            cparser::readTable(base, file, entryMacros, options);
        REQUIRE(table.has_value());
        return std::move(*table);
    }

    cparser::MacroTable base;
};

} // namespace

TEST_CASE("an ordinal table reads into rows in source order", "[cparser][table]") {
    const Harness harness;
    const cparser::TableResult table =
        harness.read(getFixtures() / "table/scene_table.h", SceneMacros);

    REQUIRE(table.rows.size() == 6);
    REQUIRE(table.rows[0].ordinal == 0);
    REQUIRE(table.rows[0].arguments[0] == "ydan_scene");
    REQUIRE(table.rows[0].arguments[2] == "SCENE_DEKU_TREE");
    REQUIRE(table.rows[5].ordinal == 5);
    REQUIRE(table.rows[5].arguments[0] == "MIZUsin_scene");
}

TEST_CASE("a table's spellings outlive the reader that made them", "[cparser][table]") {
    cparser::TableResult table;
    {
        const Harness harness;
        table = harness.read(getFixtures() / "table/scene_table.h", SceneMacros);
    }

    REQUIRE(table.rows[0].arguments[0] == "ydan_scene");
}

TEST_CASE("an argument keeps the spelling it was written with", "[cparser][table]") {
    const Harness harness;
    const cparser::TableResult table =
        harness.read(getFixtures() / "table/scene_table.h", SceneMacros);

    REQUIRE(table.rows[2].arguments.size() == 6);
    REQUIRE(table.rows[2].arguments[5] == "(1 << 2)");
}

TEST_CASE("a conditional entry still takes its ordinal", "[cparser][table]") {
    const Harness harness;
    const cparser::TableResult table =
        harness.read(getFixtures() / "table/object_table.h", ObjectMacros);

    REQUIRE(table.rows.size() == 5);
    for (cparser::usize at = 0; at < table.rows.size(); ++at) {
        REQUIRE(table.rows[at].ordinal == at);
    }
    REQUIRE(table.rows[4].arguments[0] == "object_link_boy");
}

TEST_CASE("an unevaluable condition in a table is loud, never silent", "[cparser][table]") {
    const Harness harness;
    const cparser::TableResult table =
        harness.read(getFixtures() / "table/object_table.h", ObjectMacros);

    REQUIRE(table.rows[3].macro == "DEFINE_OBJECT_UNSET");
    REQUIRE(reports(table, "OOT_DEBUG"));
    REQUIRE(std::ranges::any_of(table.diagnostics, [](const base::Diagnostic& diagnostic) {
        return diagnostic.severity == base::Severity::Warning &&
               diagnostic.message.contains("OOT_DEBUG");
    }));
}

TEST_CASE("the configuration decides which arm of the conditional is live", "[cparser][table]") {
    const Harness harness;
    const cparser::PreprocessOptions options{
        .resolve = [](std::string_view name) -> std::optional<cparser::Value> {
            return name == "OOT_DEBUG" ? std::optional<cparser::Value>(cparser::makeInteger(1))
                                       : std::nullopt;
        },
    };
    const cparser::TableResult table =
        harness.read(getFixtures() / "table/object_table.h", ObjectMacros, options);

    REQUIRE(table.rows.size() == 5);
    REQUIRE(table.rows[3].macro == "DEFINE_OBJECT");
    REQUIRE(table.rows[3].arguments[0] == "object_human_test");
    REQUIRE_FALSE(reports(table, "OOT_DEBUG"));
}
