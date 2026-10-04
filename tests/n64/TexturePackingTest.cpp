#include "n64/TexturePacking.h"

#include "base/Types.h"
#include "n64/Color.h"
#include "n64/TileDescriptor.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using base::u8;
using n64::Color;
using n64::TexelSize;
using n64::TextureFormat;

TEST_CASE("greyscale weighs green the most", "[n64][packing]") {
    REQUIRE(n64::makeGreyscale({255, 255, 255, 0}) == 255);
    REQUIRE(n64::makeGreyscale({0, 255, 0, 0}) == 182);
    REQUIRE(n64::makeGreyscale({255, 0, 0, 0}) == 54);
}

TEST_CASE("each format keeps the bits it has room for", "[n64][packing]") {
    const std::vector<Color> texels{{255, 128, 8, 200}, {0, 0, 0, 0}};

    REQUIRE(n64::packTexels(texels, TextureFormat::Rgba, TexelSize::Bits32) ==
            std::vector<u8>{255, 128, 8, 200, 0, 0, 0, 0});
    REQUIRE(n64::packTexels(texels, TextureFormat::Rgba, TexelSize::Bits16) ==
            std::vector<u8>{0xFC, 0x03, 0x00, 0x00});
    REQUIRE(n64::packTexels(texels, TextureFormat::IntensityAlpha, TexelSize::Bits16) ==
            std::vector<u8>{146, 200, 0, 0});
    REQUIRE(n64::packTexels(texels, TextureFormat::IntensityAlpha, TexelSize::Bits4) ==
            std::vector<u8>{0x90});
}

TEST_CASE("4-bit indices pack high nibble first", "[n64][packing]") {
    REQUIRE(n64::packColorIndices(std::vector<u8>{1, 2, 3}, TexelSize::Bits4) ==
            std::vector<u8>{0x12, 0x30});
    REQUIRE(n64::packColorIndices(std::vector<u8>{1, 2, 3}, TexelSize::Bits8) ==
            std::vector<u8>{1, 2, 3});
}
