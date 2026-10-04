#include "z64/TextureName.h"

#include "n64/TileDescriptor.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

TEST_CASE("a texture name reads the way the build reads it", "[z64][texture-name]") {
    const std::optional<z64::TextureName> name =
        z64::parseTextureName("gHylianOldManEyeHalfTex.ci8.tlut_gHylianOldManTLUT_u32.u32.png");

    REQUIRE(name.has_value());
    REQUIRE(name->prefix == "gHylianOldManEyeHalfTex");
    REQUIRE(name->format == n64::TextureFormat::ColorIndex);
    REQUIRE(name->size == n64::TexelSize::Bits8);
    REQUIRE(name->elementSize == 4);
    REQUIRE(name->sharedPalette == std::optional<std::string>{"gHylianOldManTLUT"});
    REQUIRE(name->paletteElementSize == 4);
}

TEST_CASE("a name without suffixes takes the build's defaults", "[z64][texture-name]") {
    const std::optional<z64::TextureName> name = z64::parseTextureName("spot00_room_0Tex.i4.png");

    REQUIRE(name.has_value());
    REQUIRE(name->prefix == "spot00_room_0Tex");
    REQUIRE(name->format == n64::TextureFormat::Intensity);
    REQUIRE(name->size == n64::TexelSize::Bits4);
    REQUIRE(name->elementSize == 8);
    REQUIRE_FALSE(name->sharedPalette.has_value());
}

TEST_CASE("a split palette half reads before the palette name", "[z64][texture-name]") {
    const std::optional<z64::TextureName> name =
        z64::parseTextureName("gNightSkybox2Tex.ci8.split_lo.tlut_gNightSkyboxTLUT.png");

    REQUIRE(name.has_value());
    REQUIRE(name->paletteHalf == z64::PaletteHalf::Low);
    REQUIRE(name->sharedPalette == std::optional<std::string>{"gNightSkyboxTLUT"});
}

TEST_CASE("names the build would refuse are not texture names", "[z64][texture-name]") {
    REQUIRE_FALSE(z64::parseTextureName("plain.png").has_value());
    REQUIRE_FALSE(z64::parseTextureName("tex.rgba16.c").has_value());
    REQUIRE_FALSE(z64::parseTextureName("tex.rgba16.split_lo.png").has_value());
    REQUIRE_FALSE(z64::parseTextureName("tex.ci8.split_lo.png").has_value());
    REQUIRE_FALSE(z64::parseTextureName("tex.ci8.tlut_.png").has_value());
}
