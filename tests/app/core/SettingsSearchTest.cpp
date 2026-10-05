#include "app/core/domain/SettingsSearch.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace {

constexpr std::array<std::string_view, 3> Jobs{"Parallel jobs",
                                               "How many recipes make runs at once.", "Build"};

} // namespace

TEST_CASE("a search matches every word anywhere in a setting, ignoring case", "[core][settings]") {
    REQUIRE(toki::core::matchesSearch(Jobs, ""));
    REQUIRE(toki::core::matchesSearch(Jobs, "  "));
    REQUIRE(toki::core::matchesSearch(Jobs, "JOBS"));
    REQUIRE(toki::core::matchesSearch(Jobs, "build parallel"));
    REQUIRE(toki::core::matchesSearch(Jobs, "make once"));
    REQUIRE_FALSE(toki::core::matchesSearch(Jobs, "jobs log"));
    REQUIRE_FALSE(toki::core::matchesSearch(Jobs, "viewport"));
}
