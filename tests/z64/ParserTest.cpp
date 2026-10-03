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

TEST_CASE("a texture's initializer holds the image the build would include", "[z64][parser]") {
    const Harness harness;
    const z64::Declaration& texture = harness.find("sFloorTex");

    REQUIRE(texture.initializer.size() == 1);
    const z64::Token& image = texture.initializer.front();
    REQUIRE(image.kind == z64::TokenKind::Image);
    REQUIRE(image.image < harness.unit.images.size());
    REQUIRE(harness.unit.images[image.image].path.filename() == "sFloorTex.rgba16.png");
}

TEST_CASE("an empty bound filled from an image is left unknown, not counted", "[z64][parser]") {
    const Harness harness;
    const z64::Declaration& palette = harness.find("sPalette");

    REQUIRE(palette.isArray);
    REQUIRE_FALSE(palette.arrayLength.has_value());
    REQUIRE(harness.unit.images[palette.initializer.front().image].format == "ci8");
}

TEST_CASE("an image nested inside an element does not stop the count", "[z64][parser]") {
    const z64::MacroTable base;
    const z64::TranslationUnit unit =
        z64::preprocessText(base, fixtures() / "asset/<memory>.c",
                            "u64 sNested[] = {\n    { 1, 2 },\n    {\n#include "
                            "\"sFloorTex.rgba16.inc.c\"\n    },\n};\n",
                            z64::PreprocessOptions{.allowedRoots = {fixtures() / "asset"}});
    const z64::ParseResult parsed = z64::parseDeclarations(unit, z64::EvalContext{});

    REQUIRE(parsed.declarations.size() == 1);
    REQUIRE(parsed.declarations.front().arrayLength == 2);
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

namespace {

[[nodiscard]] z64::ParseResult parseText(const z64::TranslationUnit& unit) {
    return z64::parseDeclarations(unit, z64::EvalContext{});
}

[[nodiscard]] z64::TranslationUnit unitOf(const z64::MacroTable& base, std::string source) {
    return z64::preprocessText(base, "memory.c", std::move(source), {});
}

[[nodiscard]] bool notes(const z64::ParseResult& parsed, std::string_view text) {
    return std::ranges::any_of(parsed.diagnostics, [text](const z64::Diagnostic& diagnostic) {
        return diagnostic.severity == z64::Severity::Note && diagnostic.message.contains(text);
    });
}

} // namespace

TEST_CASE("a function prototype is not a declaration of data", "[z64][parser]") {
    const z64::MacroTable base;
    const z64::TranslationUnit unit = unitOf(base, "void Foo_Init(void);\ns32 sValue = 1;\n");
    const z64::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.declarations.size() == 1);
    REQUIRE(parsed.declarations.front().name == "sValue");
    REQUIRE(parsed.diagnostics.empty());
}

TEST_CASE("every declarator in a list is read, with the type they share", "[z64][parser]") {
    const z64::MacroTable base;
    const z64::TranslationUnit unit = unitOf(base, "static s16 a = 1, *b, c[] = { 1, 2, 3 };\n");
    const z64::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.declarations.size() == 3);
    REQUIRE(parsed.declarations[0].name == "a");
    REQUIRE(parsed.declarations[0].initializer.size() == 1);
    REQUIRE(parsed.declarations[1].name == "b");
    REQUIRE(parsed.declarations[1].type == "s16");
    REQUIRE(parsed.declarations[1].pointerDepth == 1);
    REQUIRE(parsed.declarations[2].name == "c");
    REQUIRE(parsed.declarations[2].arrayLength == 3);
}

TEST_CASE("an attribute does not hide the name it decorates", "[z64][parser]") {
    const z64::MacroTable base;
    const z64::TranslationUnit unit = unitOf(
        base,
        "__attribute__((aligned(8))) u64 sTex[2] __attribute__((aligned (16))) = { 0, 0 };\n");
    const z64::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.declarations.size() == 1);
    REQUIRE(parsed.declarations.front().name == "sTex");
    REQUIRE(parsed.declarations.front().type == "u64");
    REQUIRE(parsed.declarations.front().arrayLength == 2);
}

TEST_CASE("a bound naming an unknown macro falls back to counting the initializer",
          "[z64][parser]") {
    const z64::MacroTable base;
    const z64::TranslationUnit unit = unitOf(base, "u8 sArr[LENGTH_UNKNOWN] = { 1, 2, 3 };\n");
    const z64::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.declarations.front().arrayLength == 3);
    REQUIRE(std::ranges::any_of(parsed.diagnostics, [](const z64::Diagnostic& diagnostic) {
        return diagnostic.severity == z64::Severity::Warning &&
               diagnostic.message.contains("LENGTH_UNKNOWN");
    }));
}

TEST_CASE("a bound naming an unknown macro with nothing to count is unknown", "[z64][parser]") {
    const z64::MacroTable base;
    const z64::TranslationUnit unit = unitOf(base, "extern u8 sArr[LENGTH_UNKNOWN];\n");

    REQUIRE_FALSE(parseText(unit).declarations.front().arrayLength.has_value());
}

TEST_CASE("what the parser cannot read is noted, never dropped silently", "[z64][parser]") {
    const z64::MacroTable base;

    const z64::TranslationUnit pointer = unitOf(base, "void (*sCallback)(void) = 0;\n");
    REQUIRE(notes(parseText(pointer), "function pointer"));

    const z64::TranslationUnit unclosed = unitOf(base, "s32 sValue = 1\n");
    REQUIRE(notes(parseText(unclosed), "never closed"));
}
