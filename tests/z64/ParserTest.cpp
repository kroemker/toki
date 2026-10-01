#include "z64/Parser.h"

#include "z64/Diagnostic.h"
#include "z64/Expression.h"
#include "z64/Initializer.h"
#include "z64/MacroTable.h"
#include "z64/Preprocessor.h"
#include "z64/Token.h"
#include "z64/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "z64/fixtures";
}

class Harness {
public:
    Harness() {
        std::expected<z64::TranslationUnit, z64::Diagnostic> read =
            z64::preprocess(base, fixtures() / "asset/room.c",
                            z64::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});
        REQUIRE(read.has_value());
        unit = std::move(*read);
        parsed = z64::parseDeclarations(unit, z64::EvalContext{});
    }

    /// Prefers the definition over the `extern` the header declared first.
    [[nodiscard]] const z64::Declaration& find(std::string_view name) const {
        const z64::Declaration* match = nullptr;
        for (const z64::Declaration& declaration : parsed.declarations) {
            if (declaration.name != name) {
                continue;
            }
            if (match == nullptr || (!match->hasInitializer && declaration.hasInitializer)) {
                match = &declaration;
            }
        }
        REQUIRE(match != nullptr);
        return *match;
    }

    [[nodiscard]] z64::usize countOf(std::string_view name) const {
        return static_cast<z64::usize>(
            std::ranges::count_if(parsed.declarations, [name](const z64::Declaration& declaration) {
                return declaration.name == name;
            }));
    }

    z64::MacroTable base;
    z64::TranslationUnit unit;
    z64::ParseResult parsed;
};

} // namespace

TEST_CASE("a definition carries its type, name and element count", "[z64][parser]") {
    const Harness harness;
    const z64::Declaration& vertices = harness.find("sVertices");

    REQUIRE(vertices.type == "Vtx");
    REQUIRE(vertices.isArray);
    REQUIRE(vertices.hasInitializer);
    REQUIRE(vertices.arrayLength == 2);
    REQUIRE(vertices.pointerDepth == 0);
}

TEST_CASE("an array bound written as a macro is evaluated", "[z64][parser]") {
    const Harness harness;

    REQUIRE(harness.find("sActorList").arrayLength == 2);
}

TEST_CASE("a texture's length comes from TEX_LEN, not from its data", "[z64][parser]") {
    const Harness harness;
    const z64::Declaration& texture = harness.find("sFloorTex");

    REQUIRE(texture.type == "u64");
    REQUIRE(texture.arrayLength == 256);
}

TEST_CASE("an empty bound is counted from the initializer", "[z64][parser]") {
    const Harness harness;
    const z64::Declaration& commands = harness.find("sOpaque");

    REQUIRE(commands.type == "Gfx");
    REQUIRE(commands.arrayLength == 3);
}

TEST_CASE("the extern in the header and the definition are both seen", "[z64][parser]") {
    const Harness harness;

    REQUIRE(harness.countOf("sVertices") == 2);
    REQUIRE(harness.parsed.declarations.front().isExtern);
}

TEST_CASE("a conditional entry excluded by the config is not counted", "[z64][parser]") {
    const Harness harness;
    const z64::Declaration& actors = harness.find("sActorList");
    REQUIRE(actors.hasInitializer);

    REQUIRE(z64::countElements(actors.initializer) == 1);
}

TEST_CASE("an initializer is kept as the tokens it was written as", "[z64][parser]") {
    const Harness harness;
    const z64::Declaration& commands = harness.find("sOpaque");
    const std::vector<std::span<const z64::Token>> elements =
        z64::splitElements(commands.initializer);

    REQUIRE(elements.size() == 3);
    REQUIRE(elements[0].front().text == "gsSPVertex");
    REQUIRE(elements[2].front().text == "gsSPEndDisplayList");
}

TEST_CASE("a scalar definition is read too", "[z64][parser]") {
    const Harness harness;
    const z64::Declaration& angle = harness.find("sAngle");

    REQUIRE(angle.type == "s16");
    REQUIRE_FALSE(angle.isArray);
    REQUIRE(angle.hasInitializer);
}
