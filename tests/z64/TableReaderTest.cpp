#include "z64/TableReader.h"

#include "z64/Diagnostic.h"
#include "z64/Expression.h"
#include "z64/MacroTable.h"
#include "z64/Preprocessor.h"
#include "z64/Types.h"
#include "z64/Value.h"

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

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "z64/fixtures";
}

[[nodiscard]] bool reports(const z64::TableResult& table, std::string_view text) {
    return std::ranges::any_of(table.diagnostics, [text](const z64::Diagnostic& diagnostic) {
        return diagnostic.message.contains(text);
    });
}

class Harness {
public:
    [[nodiscard]] z64::TableResult read(const std::filesystem::path& file,
                                        std::span<const std::string_view> entryMacros,
                                        const z64::PreprocessOptions& options = {}) const {
        std::expected<z64::TableResult, z64::Diagnostic> table =
            z64::readTable(base, file, entryMacros, options);
        REQUIRE(table.has_value());
        return std::move(*table);
    }

    z64::MacroTable base;
};

} // namespace

TEST_CASE("an ordinal table reads into rows in source order", "[z64][table]") {
    const Harness harness;
    const z64::TableResult table = harness.read(fixtures() / "table/scene_table.h", SceneMacros);

    REQUIRE(table.rows.size() == 6);
    REQUIRE(table.rows[0].ordinal == 0);
    REQUIRE(table.rows[0].arguments[0] == "ydan_scene");
    REQUIRE(table.rows[0].arguments[2] == "SCENE_DEKU_TREE");
    REQUIRE(table.rows[5].ordinal == 5);
    REQUIRE(table.rows[5].arguments[0] == "MIZUsin_scene");
}

TEST_CASE("a table's spellings outlive the reader that made them", "[z64][table]") {
    z64::TableResult table;
    {
        const Harness harness;
        table = harness.read(fixtures() / "table/scene_table.h", SceneMacros);
    }

    REQUIRE(table.rows[0].arguments[0] == "ydan_scene");
}

TEST_CASE("an argument keeps the spelling it was written with", "[z64][table]") {
    const Harness harness;
    const z64::TableResult table = harness.read(fixtures() / "table/scene_table.h", SceneMacros);

    REQUIRE(table.rows[2].arguments.size() == 6);
    REQUIRE(table.rows[2].arguments[5] == "(1 << 2)");
}

TEST_CASE("a conditional entry still takes its ordinal", "[z64][table]") {
    const Harness harness;
    const z64::TableResult table = harness.read(fixtures() / "table/object_table.h", ObjectMacros);

    REQUIRE(table.rows.size() == 5);
    for (z64::usize at = 0; at < table.rows.size(); ++at) {
        REQUIRE(table.rows[at].ordinal == at);
    }
    REQUIRE(table.rows[4].arguments[0] == "object_link_boy");
}

TEST_CASE("an unevaluable condition in a table is loud, never silent", "[z64][table]") {
    const Harness harness;
    const z64::TableResult table = harness.read(fixtures() / "table/object_table.h", ObjectMacros);

    REQUIRE(table.rows[3].macro == "DEFINE_OBJECT_UNSET");
    REQUIRE(reports(table, "OOT_DEBUG"));
    REQUIRE(std::ranges::any_of(table.diagnostics, [](const z64::Diagnostic& diagnostic) {
        return diagnostic.severity == z64::Severity::Warning &&
               diagnostic.message.contains("OOT_DEBUG");
    }));
}

TEST_CASE("the configuration decides which arm of the conditional is live", "[z64][table]") {
    const Harness harness;
    const z64::PreprocessOptions options{
        .resolve = [](std::string_view name) -> std::optional<z64::Value> {
            return name == "OOT_DEBUG" ? std::optional<z64::Value>(z64::integerValue(1))
                                       : std::nullopt;
        },
    };
    const z64::TableResult table =
        harness.read(fixtures() / "table/object_table.h", ObjectMacros, options);

    REQUIRE(table.rows.size() == 5);
    REQUIRE(table.rows[3].macro == "DEFINE_OBJECT");
    REQUIRE(table.rows[3].arguments[0] == "object_human_test");
    REQUIRE_FALSE(reports(table, "OOT_DEBUG"));
}
