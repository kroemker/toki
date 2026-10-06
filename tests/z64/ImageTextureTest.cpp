#include "z64/ImageTexture.h"

#include "n64/TileDescriptor.h"
#include "z64/TextureIncludes.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

TEST_CASE("an embedded image is a texture of the format its name gives", "[z64][image-texture]") {
    const std::optional<z64::ImageTexture> texture =
        z64::makeImageTexture("gEyeTex.ia8.png", z64::ImageTag, 32, 16);

    REQUIRE(texture.has_value());
    REQUIRE_FALSE(texture->isPalette);
    REQUIRE(texture->format == n64::TextureFormat::IntensityAlpha);
    REQUIRE(texture->size == n64::TexelSize::Bits8);
    REQUIRE(texture->width == 32);
    REQUIRE(texture->height == 16);
    REQUIRE(texture->paletteName.empty());
}

TEST_CASE("a colour-indexed image and its palette share a palette name", "[z64][image-texture]") {
    const std::optional<z64::ImageTexture> own =
        z64::makeImageTexture("gCrateTex.ci4.png", z64::ImageTag, 16, 16);
    const std::optional<z64::ImageTexture> ownPalette =
        z64::makeImageTexture("gCrateTex.ci4.png", z64::PaletteTag, 16, 16);
    const std::optional<z64::ImageTexture> shared =
        z64::makeImageTexture("gWallTex.ci8.tlut_gWallTLUT.png", z64::ImageTag, 32, 32);
    const std::optional<z64::ImageTexture> sharedPalette =
        z64::makeImageTexture("gFloorTex.ci8.tlut_gWallTLUT.png", z64::PaletteTag, 32, 32);

    REQUIRE(own->paletteName == "gCrateTex");
    REQUIRE(ownPalette->isPalette);
    REQUIRE(ownPalette->paletteName == "gCrateTex");
    REQUIRE(ownPalette->format == n64::TextureFormat::Rgba);
    REQUIRE(shared->paletteName == "gWallTLUT");
    REQUIRE(sharedPalette->paletteName == "gWallTLUT");
}

TEST_CASE("files that are not textures make none", "[z64][image-texture]") {
    REQUIRE_FALSE(z64::makeImageTexture("sData.bin", z64::BinaryTag, 0, 0).has_value());
    REQUIRE_FALSE(z64::makeImageTexture("sBackground.jpg", z64::JpegTag, 0, 0).has_value());
    REQUIRE_FALSE(z64::makeImageTexture("notATexture.png", z64::ImageTag, 0, 0).has_value());
}
