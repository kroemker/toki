#include "hackeroot/BuildDefines.h"

#include "base/Diagnostic.h"
#include "makeparser/Makefile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path getCheckout() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

[[nodiscard]] hackeroot::BuildDefines read(const makeparser::MakeOverrides& settings = {}) {
    std::expected<hackeroot::BuildDefines, base::Diagnostic> defines =
        hackeroot::readBuildDefines(getCheckout(), settings);
    REQUIRE(defines.has_value());
    return std::move(*defines);
}

[[nodiscard]] bool passes(const hackeroot::BuildDefines& defines, std::string_view argument) {
    return std::ranges::find(defines.arguments, argument) != defines.arguments.end();
}

} // namespace

TEST_CASE("the defines come out of the Makefile in the order the build passes them",
          "[hackeroot][defines]") {
    const hackeroot::BuildDefines defines = read();

    REQUIRE(defines.arguments == std::vector<std::string>{
                                     "-DPACKAGE_NAME=\"\"",
                                     "-DTESTSUITE_MODE=0",
                                     "-DPLATFORM_N64=1",
                                     "-DOOT_VERSION=NTSC_1_2",
                                     "-DDEBUG_FEATURES=1",
                                     "-DF3DEX_GBI_2",
                                 });
}

TEST_CASE("settings are make's command line, and what depends on them follows",
          "[hackeroot][defines]") {
    const hackeroot::BuildDefines defines =
        read({{"VERSION", "gc-eu-mq-dbg"}, {"DEBUG_FEATURES", "0"}});

    REQUIRE(passes(defines, "-DPLATFORM_N64=0"));
    REQUIRE(passes(defines, "-DOOT_VERSION=GC_EU_MQ_DBG"));
    REQUIRE(passes(defines, "-DNDEBUG"));
    REQUIRE(passes(defines, "-DDEBUG_FEATURES=0"));
}

TEST_CASE("the linker map is where the Makefile has the build write it", "[hackeroot][defines]") {
    REQUIRE(read().mapFile == getCheckout() / "build/ntsc-1.2/hackeroot-ntsc-1.2.map");
    REQUIRE(read({{"VERSION", "gc-eu-mq-dbg"}}).mapFile ==
            getCheckout() / "build/gc-eu-mq-dbg/hackeroot-gc-eu-mq-dbg.map");
}

TEST_CASE("an explicit VERSION_MACRO is left alone", "[hackeroot][defines]") {
    const hackeroot::BuildDefines defines = read({{"VERSION_MACRO", "SOMETHING_ELSE"}});

    REQUIRE(passes(defines, "-DOOT_VERSION=SOMETHING_ELSE"));
}

TEST_CASE("a define fed by a shell command is named in a note", "[hackeroot][defines]") {
    const hackeroot::BuildDefines defines = read();

    REQUIRE(std::ranges::any_of(defines.diagnostics, [](const base::Diagnostic& diagnostic) {
        return diagnostic.severity == base::Severity::Note &&
               diagnostic.message.contains("-DPACKAGE_NAME");
    }));
    REQUIRE_FALSE(std::ranges::any_of(defines.diagnostics, [](const base::Diagnostic& diagnostic) {
        return diagnostic.severity != base::Severity::Note;
    }));
}

TEST_CASE("a checkout without a Makefile is an error", "[hackeroot][defines]") {
    REQUIRE_FALSE(hackeroot::readBuildDefines(getCheckout() / "include", {}).has_value());
}
