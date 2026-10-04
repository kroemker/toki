#include "z64/TextureIncludes.h"

#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>

namespace {

[[nodiscard]] std::filesystem::path getTextures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "z64/fixtures/textures";
}

[[nodiscard]] std::optional<cparser::ResolvedInclude> resolve(const char* spelling) {
    cparser::DirectoryCache directories;
    return z64::resolveTextureInclude(getTextures() / spelling, directories);
}

} // namespace

TEST_CASE("a generated texture include resolves to the image it comes from", "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> resolved = resolve("sFloorTex.rgba16.inc.c");

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->embedded);
    REQUIRE(resolved->path.filename() == "sFloorTex.rgba16.png");
    REQUIRE(resolved->tag == z64::ImageTag);
}

TEST_CASE("a paletted image names its palette, so the include is only a prefix",
          "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> resolved = resolve("sPalette.ci8.inc.c");

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->path.filename() == "sPalette.ci8.tlut_sRoomTlut.png");
    REQUIRE(resolved->tag == z64::ImageTag);
}

TEST_CASE("a generated binary include resolves to the binary", "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> resolved = resolve("sData.bin.inc.c");

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->path.filename() == "sData.bin");
    REQUIRE(resolved->tag == z64::BinaryTag);
}

TEST_CASE("a palette include resolves to an image that uses the palette", "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> shared = resolve("sShared.tlut.rgba16.inc.c");
    const std::optional<cparser::ResolvedInclude> own = resolve("sCrate.tlut.rgba16.inc.c");

    REQUIRE(shared.has_value());
    REQUIRE(shared->tag == z64::PaletteTag);
    REQUIRE(shared->path.filename().string().contains("tlut_sShared"));
    REQUIRE(own.has_value());
    REQUIRE(own->path.filename() == "sCrate.ci4.png");
    REQUIRE_FALSE(resolve("sNobody.tlut.rgba16.inc.c").has_value());
}

TEST_CASE("a u32 palette include belongs to the images naming a u32 palette", "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> narrow = resolve("sOdd.tlut.rgba16.u32.inc.c");
    const std::optional<cparser::ResolvedInclude> wide = resolve("sOdd.tlut.rgba16.inc.c");

    REQUIRE(narrow.has_value());
    REQUIRE(narrow->path.filename() == "sHalfB.ci8.tlut_sOdd_u32.png");
    REQUIRE(wide.has_value());
    REQUIRE(wide->path.filename() == "sHalfA.ci8.tlut_sOdd.png");
}

TEST_CASE("a generated JPEG include resolves to the JPEG", "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> resolved = resolve("sBackground.jpg.inc.c");

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->path.filename() == "sBackground.jpg");
    REQUIRE(resolved->tag == z64::JpegTag);
}

TEST_CASE("a name that only extends the spelling is not its image", "[z64][textures]") {
    REQUIRE_FALSE(resolve("sTile.rgba16.inc.c").has_value());
}

TEST_CASE("only a generated include is a texture", "[z64][textures]") {
    REQUIRE_FALSE(resolve("sFloorTex.rgba16.h").has_value());
    REQUIRE_FALSE(resolve("sMissing.rgba16.inc.c").has_value());
}

TEST_CASE("the preprocessor embeds a texture the rule resolves", "[z64][textures]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit = cparser::preprocessText(
        base, getTextures() / "<memory>",
        "u64 sTex[] = {\n#include \"sFloorTex.rgba16.inc.c\"\n};\n",
        cparser::PreprocessOptions{.resolveMissingInclude = z64::resolveTextureInclude});

    REQUIRE(unit.embedded.size() == 1);
    REQUIRE(unit.embedded.front().path.filename() == "sFloorTex.rgba16.png");
    REQUIRE(unit.embedded.front().tag == z64::ImageTag);
}
