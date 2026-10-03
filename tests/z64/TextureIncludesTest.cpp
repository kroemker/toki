#include "z64/TextureIncludes.h"

#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>

namespace {

[[nodiscard]] std::filesystem::path textures() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "z64/fixtures/textures";
}

[[nodiscard]] std::optional<cparser::ResolvedInclude> resolve(const char* spelling) {
    cparser::DirectoryCache directories;
    return z64::resolveTextureInclude(textures() / spelling, directories);
}

} // namespace

TEST_CASE("a generated texture include resolves to the image it comes from", "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> resolved = resolve("sFloorTex.rgba16.inc.c");

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->embedded);
    REQUIRE(resolved->path.filename() == "sFloorTex.rgba16.png");
    REQUIRE(resolved->tag == "rgba16");
}

TEST_CASE("a paletted image names its palette, so the include is only a prefix",
          "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> resolved = resolve("sPalette.ci8.inc.c");

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->path.filename() == "sPalette.ci8.tlut_sRoomTlut.png");
    REQUIRE(resolved->tag == "ci8");
}

TEST_CASE("a generated binary include resolves to the binary", "[z64][textures]") {
    const std::optional<cparser::ResolvedInclude> resolved = resolve("sData.bin.inc.c");

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->path.filename() == "sData.bin");
    REQUIRE(resolved->tag == "bin");
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
        base, textures() / "<memory>", "u64 sTex[] = {\n#include \"sFloorTex.rgba16.inc.c\"\n};\n",
        cparser::PreprocessOptions{.missingInclude = z64::resolveTextureInclude});

    REQUIRE(unit.embedded.size() == 1);
    REQUIRE(unit.embedded.front().path.filename() == "sFloorTex.rgba16.png");
    REQUIRE(unit.embedded.front().tag == "rgba16");
}
