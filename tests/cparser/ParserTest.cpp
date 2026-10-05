#include "cparser/Parser.h"

#include "Generated.h"
#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/Initializer.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path getFixtures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "cparser/fixtures";
}

class Harness {
public:
    Harness() {
        std::expected<cparser::TranslationUnit, base::Diagnostic> read = cparser::preprocess(
            base, getFixtures() / "asset/room.c",
            cparser::PreprocessOptions{.allowedRoots = {getFixtures() / "asset"},
                                       .resolveMissingInclude = embedGenerated});
        REQUIRE(read.has_value());
        unit = std::move(*read);
        parsed = cparser::parseDeclarations(unit, cparser::EvalContext{});
    }

    /// Prefers the definition over the `extern` the header declared first.
    [[nodiscard]] const cparser::Declaration& find(std::string_view name) const {
        const cparser::Declaration* match = nullptr;
        for (const cparser::Declaration& declaration : parsed.declarations) {
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

    [[nodiscard]] cparser::usize countNamed(std::string_view name) const {
        return static_cast<cparser::usize>(std::ranges::count_if(
            parsed.declarations,
            [name](const cparser::Declaration& declaration) { return declaration.name == name; }));
    }

    cparser::MacroTable base;
    cparser::TranslationUnit unit;
    cparser::ParseResult parsed;
};

} // namespace

TEST_CASE("a definition carries its type, name and element count", "[cparser][parser]") {
    const Harness harness;
    const cparser::Declaration& vertices = harness.find("sVertices");

    REQUIRE(vertices.type == "Vtx");
    REQUIRE(vertices.isArray);
    REQUIRE(vertices.hasInitializer);
    REQUIRE(vertices.arrayLength == 2);
    REQUIRE(vertices.pointerDepth == 0);
}

TEST_CASE("an array bound written as a macro is evaluated", "[cparser][parser]") {
    const Harness harness;

    REQUIRE(harness.find("sActorList").arrayLength == 2);
}

TEST_CASE("a declared length stands although the data is embedded", "[cparser][parser]") {
    const Harness harness;
    const cparser::Declaration& texture = harness.find("sFloorTex");

    REQUIRE(texture.type == "u64");
    REQUIRE(texture.arrayLength == 256);
}

TEST_CASE("a texture's initializer holds the image the build would include", "[cparser][parser]") {
    const Harness harness;
    const cparser::Declaration& texture = harness.find("sFloorTex");

    REQUIRE(texture.initializer.size() == 1);
    const cparser::Token& image = texture.initializer.front();
    REQUIRE(image.kind == cparser::TokenKind::Embedded);
    REQUIRE(image.embedded < harness.unit.embedded.size());
    REQUIRE(harness.unit.embedded[image.embedded].path.filename() == "sFloorTex.rgba16.inc.c");
}

TEST_CASE("an empty bound filled from an image is left unknown, not counted", "[cparser][parser]") {
    const Harness harness;
    const cparser::Declaration& palette = harness.find("sPalette");

    REQUIRE(palette.isArray);
    REQUIRE_FALSE(palette.arrayLength.has_value());
    REQUIRE(harness.unit.embedded[palette.initializer.front().embedded].tag == "generated");
}

TEST_CASE("an image nested inside an element does not stop the count", "[cparser][parser]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit = cparser::preprocessText(
        base, getFixtures() / "asset/<memory>.c",
        "u64 sNested[] = {\n    { 1, 2 },\n    {\n#include "
        "\"sFloorTex.rgba16.inc.c\"\n    },\n};\n",
        cparser::PreprocessOptions{.allowedRoots = {getFixtures() / "asset"},
                                   .resolveMissingInclude = embedGenerated});
    const cparser::ParseResult parsed = cparser::parseDeclarations(unit, cparser::EvalContext{});

    REQUIRE(parsed.declarations.size() == 1);
    REQUIRE(parsed.declarations.front().arrayLength == 2);
}

TEST_CASE("an empty bound is counted from the initializer", "[cparser][parser]") {
    const Harness harness;
    const cparser::Declaration& commands = harness.find("sOpaque");

    REQUIRE(commands.type == "Gfx");
    REQUIRE(commands.arrayLength == 3);
}

TEST_CASE("the extern in the header and the definition are both seen", "[cparser][parser]") {
    const Harness harness;

    REQUIRE(harness.countNamed("sVertices") == 2);
    REQUIRE(harness.parsed.declarations.front().isExtern);
}

TEST_CASE("a conditional entry excluded by the config is not counted", "[cparser][parser]") {
    const Harness harness;
    const cparser::Declaration& actors = harness.find("sActorList");
    REQUIRE(actors.hasInitializer);

    REQUIRE(cparser::countElements(actors.initializer) == 1);
}

TEST_CASE("an initializer is kept as the tokens it was written as", "[cparser][parser]") {
    const Harness harness;
    const cparser::Declaration& commands = harness.find("sOpaque");
    const std::vector<std::span<const cparser::Token>> elements =
        cparser::splitElements(commands.initializer);

    REQUIRE(elements.size() == 3);
    REQUIRE(elements[0].front().text == "gsSPVertex");
    REQUIRE(elements[2].front().text == "gsSPEndDisplayList");
}

TEST_CASE("a scalar definition is read too", "[cparser][parser]") {
    const Harness harness;
    const cparser::Declaration& angle = harness.find("sAngle");

    REQUIRE(angle.type == "s16");
    REQUIRE_FALSE(angle.isArray);
    REQUIRE(angle.hasInitializer);
}

namespace {

[[nodiscard]] cparser::ParseResult parseText(const cparser::TranslationUnit& unit) {
    return cparser::parseDeclarations(unit, cparser::EvalContext{});
}

[[nodiscard]] cparser::TranslationUnit preprocessSource(const cparser::MacroTable& base,
                                                        std::string source) {
    return cparser::preprocessText(base, "memory.c", std::move(source), {});
}

[[nodiscard]] bool notes(const cparser::ParseResult& parsed, std::string_view text) {
    return std::ranges::any_of(parsed.diagnostics, [text](const base::Diagnostic& diagnostic) {
        return diagnostic.severity == base::Severity::Note && diagnostic.message.contains(text);
    });
}

} // namespace

TEST_CASE("a function prototype is not a declaration of data", "[cparser][parser]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        preprocessSource(base, "void Foo_Init(void);\ns32 sValue = 1;\n");
    const cparser::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.declarations.size() == 1);
    REQUIRE(parsed.declarations.front().name == "sValue");
    REQUIRE(parsed.diagnostics.empty());
}

TEST_CASE("every declarator in a list is read, with the type they share", "[cparser][parser]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        preprocessSource(base, "static s16 a = 1, *b, c[] = { 1, 2, 3 };\n");
    const cparser::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.declarations.size() == 3);
    REQUIRE(parsed.declarations[0].name == "a");
    REQUIRE(parsed.declarations[0].initializer.size() == 1);
    REQUIRE(parsed.declarations[1].name == "b");
    REQUIRE(parsed.declarations[1].type == "s16");
    REQUIRE(parsed.declarations[1].pointerDepth == 1);
    REQUIRE(parsed.declarations[2].name == "c");
    REQUIRE(parsed.declarations[2].arrayLength == 3);
}

TEST_CASE("an attribute does not hide the name it decorates", "[cparser][parser]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit = preprocessSource(
        base,
        "__attribute__((aligned(8))) u64 sTex[2] __attribute__((aligned (16))) = { 0, 0 };\n");
    const cparser::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.declarations.size() == 1);
    REQUIRE(parsed.declarations.front().name == "sTex");
    REQUIRE(parsed.declarations.front().type == "u64");
    REQUIRE(parsed.declarations.front().arrayLength == 2);
}

TEST_CASE("a bound naming an unknown macro falls back to counting the initializer",
          "[cparser][parser]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        preprocessSource(base, "u8 sArr[LENGTH_UNKNOWN] = { 1, 2, 3 };\n");
    const cparser::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.declarations.front().arrayLength == 3);
    REQUIRE(std::ranges::any_of(parsed.diagnostics, [](const base::Diagnostic& diagnostic) {
        return diagnostic.severity == base::Severity::Warning &&
               diagnostic.message.contains("LENGTH_UNKNOWN");
    }));
}

TEST_CASE("a bound naming an unknown macro with nothing to count is unknown", "[cparser][parser]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        preprocessSource(base, "extern u8 sArr[LENGTH_UNKNOWN];\n");

    REQUIRE_FALSE(parseText(unit).declarations.front().arrayLength.has_value());
}

TEST_CASE("what the parser cannot read is noted, never dropped silently", "[cparser][parser]") {
    const cparser::MacroTable base;

    const cparser::TranslationUnit pointer =
        preprocessSource(base, "void (*sCallback)(void) = 0;\n");
    REQUIRE(notes(parseText(pointer), "function pointer"));

    const cparser::TranslationUnit unclosed = preprocessSource(base, "s32 sValue = 1\n");
    REQUIRE(notes(parseText(unclosed), "never closed"));
}

TEST_CASE("every dimension of an array is read, the first as its length", "[cparser][parser]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit =
        preprocessSource(base, "#define ROWS 3\nu8 sTable[ROWS][2][4] = { 0 };\n");
    const cparser::ParseResult parsed = parseText(unit);

    REQUIRE(parsed.diagnostics.empty());
    REQUIRE(parsed.declarations.size() == 1);
    REQUIRE(parsed.declarations.front().arrayLength == 3);
    REQUIRE(parsed.declarations.front().innerLengths == std::vector<cparser::usize>{2, 4});
}
