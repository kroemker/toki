#include "hackeroot/Gbi.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/Workspace.h"
#include "z64/DisplayList.h"
#include "z64/DisplayListEncoder.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using base::u8;

namespace {

std::filesystem::path getCheckout() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

std::optional<cparser::SymbolAddress> findSymbol(const std::string_view name) {
    if (name == "sVtx") {
        return cparser::SymbolAddress{.address = 0x06000000, .elementSize = 16};
    }
    if (name == "sTex") {
        return cparser::SymbolAddress{.address = 0x06000800, .elementSize = 8};
    }
    return std::nullopt;
}

struct Emission {
    explicit Emission(const std::string& initializer) {
        std::expected<hackeroot::Workspace, base::Diagnostic> loaded =
            hackeroot::Workspace::load(getCheckout(), {});
        REQUIRE(loaded.has_value());
        workspace.emplace(std::move(*loaded));
        std::expected<hackeroot::Gbi, base::Diagnostic> read = hackeroot::readGbi(*workspace);
        INFO((read ? std::string() : read.error().message));
        REQUIRE(read.has_value());
        gbi.emplace(std::move(*read));
        source = cparser::preprocessText(empty, "dl.c", initializer, {});
        list = z64::DisplayListEncoder(
                   z64::DisplayListContext{.macros = &gbi->macros, .findSymbol = &findSymbol})
                   .encode(std::span(source.tokens).first(source.tokens.size() - 1));
    }

    std::optional<hackeroot::Workspace> workspace;
    std::optional<hackeroot::Gbi> gbi;
    cparser::MacroTable empty;
    cparser::TranslationUnit source;
    z64::DisplayList list;
};

} // namespace

TEST_CASE("the checkout's gbi.h encodes the commands under the build's GBI_DEFINES",
          "[hackeroot][gbi]") {
    const Emission emission("gsSPVertex(&sVtx[2], 3, 0),\n"
                            "gsSP2Triangles(0, 1, 2, 0, 3, 4, 5, 0),\n"
                            "gsSPEndDisplayList(),\n");

    REQUIRE(emission.list.diagnostics.empty());
    REQUIRE(emission.list.bytes == std::vector<u8>{
                                       0x01, 0x00, 0x30, 0x06, 0x06, 0x00, 0x00, 0x20,
                                       0x06, 0x00, 0x02, 0x04, 0x00, 0x06, 0x08, 0x0A,
                                       0xDF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   });
    REQUIRE(emission.list.commands.size() == 3);
    REQUIRE(emission.list.commands[1].macro == "gsSP2Triangles");
    REQUIRE(emission.list.commands[1].location.line == 2);
}

TEST_CASE("a macro that pastes tokens can make several commands", "[hackeroot][gbi]") {
    const Emission emission("gsDPLoadTwice(G_IM_SIZ_16b, sTex)");

    REQUIRE(emission.list.diagnostics.empty());
    REQUIRE(emission.list.commands.size() == 1);
    REQUIRE(emission.list.commands[0].commandCount == 2);
    REQUIRE(emission.list.bytes == std::vector<u8>{
                                       0xFD,
                                       0x10,
                                       0x00,
                                       0x00,
                                       0x06,
                                       0x00,
                                       0x08,
                                       0x00,
                                       0xFD,
                                       0x10,
                                       0x00,
                                       0x00,
                                       0x06,
                                       0x00,
                                       0x08,
                                       0x00,
                                   });
}

TEST_CASE("words without braces pair up into commands, as C fills an array", "[hackeroot][gbi]") {
    const Emission emission("gsSPRectangle(0x12, 7)");

    REQUIRE(emission.list.diagnostics.empty());
    REQUIRE(emission.list.commands.front().commandCount == 2);
    REQUIRE(emission.list.bytes == std::vector<u8>{
                                       0xE4,
                                       0x00,
                                       0x00,
                                       0x12,
                                       0x00,
                                       0x00,
                                       0x00,
                                       0x00,
                                       0xE1,
                                       0x00,
                                       0x00,
                                       0x00,
                                       0x00,
                                       0x00,
                                       0x00,
                                       0x07,
                                   });
}

TEST_CASE("an element that is no gbi macro is an error naming it", "[hackeroot][gbi]") {
    const Emission emission("gsSPMissing(1), gsSPEndDisplayList()");

    REQUIRE(emission.list.commands.size() == 1);
    REQUIRE(emission.list.bytes.size() == 8);
    REQUIRE(emission.list.diagnostics.size() == 1);
    REQUIRE(emission.list.diagnostics.front().severity == base::Severity::Error);
    REQUIRE(emission.list.diagnostics.front().message.contains("gsSPMissing"));
}

TEST_CASE("a macro only another configuration defines is no gbi macro", "[hackeroot][gbi]") {
    const Emission otherArm("gsSPOldOnly()");
    const Emission otherHeader("gsSPTriangleStrip(0, 1, 2)");

    REQUIRE(otherArm.list.commands.empty());
    REQUIRE(otherArm.list.diagnostics.front().message.contains("gsSPOldOnly"));
    REQUIRE(otherHeader.list.commands.empty());
    REQUIRE(otherHeader.list.diagnostics.front().message.contains("gsSPTriangleStrip"));
}

TEST_CASE("a symbol the list points at that nothing defines is reported", "[hackeroot][gbi]") {
    const Emission emission("gsSPVertex(sMissing, 3, 0)");

    REQUIRE(emission.list.commands.size() == 1);
    REQUIRE_FALSE(emission.list.diagnostics.empty());
    REQUIRE(emission.list.diagnostics.front().message.contains("sMissing"));
}

TEST_CASE("a checkout that enables F3DEX3 is refused", "[hackeroot][gbi]") {
    std::expected<hackeroot::Workspace, base::Diagnostic> workspace =
        hackeroot::Workspace::load(getCheckout(), {{"CPP_DEFINES", "-DENABLE_F3DEX3=1"}});
    REQUIRE(workspace.has_value());

    const std::expected<hackeroot::Gbi, base::Diagnostic> gbi = hackeroot::readGbi(*workspace);

    REQUIRE_FALSE(gbi.has_value());
    REQUIRE(gbi.error().message.contains("F3DEX3"));
}
