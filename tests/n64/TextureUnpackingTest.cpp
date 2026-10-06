#include "n64/TextureUnpacking.h"

#include "base/Types.h"
#include "n64/Color.h"
#include "n64/TexturePacking.h"
#include "n64/TileDescriptor.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using base::u8;
using n64::Color;
using n64::TexelSize;
using n64::TextureFormat;
using n64::TlutMode;

TEST_CASE("unpacking widens each channel by repeating its top bits", "[n64][unpacking]") {
    REQUIRE(n64::unpackTexels(std::vector<u8>{0xFC, 0x03}, TextureFormat::Rgba, TexelSize::Bits16,
                              1) == std::vector<Color>{{255, 132, 8, 255}});
    REQUIRE(n64::unpackTexels(std::vector<u8>{0xF3}, TextureFormat::IntensityAlpha,
                              TexelSize::Bits8, 1) == std::vector<Color>{{255, 255, 255, 0x33}});
    REQUIRE(n64::unpackTexels(std::vector<u8>{0xE1}, TextureFormat::IntensityAlpha,
                              TexelSize::Bits4,
                              2) == std::vector<Color>{{255, 255, 255, 0}, {0, 0, 0, 255}});
    REQUIRE(n64::unpackTexels(std::vector<u8>{0x80}, TextureFormat::Intensity, TexelSize::Bits8,
                              1) == std::vector<Color>{{0x80, 0x80, 0x80, 0x80}});
}

TEST_CASE("what the build packs unpacks to the colours it kept", "[n64][unpacking]") {
    const std::vector<Color> texels{{255, 0, 0, 255}, {0, 255, 0, 0}, {255, 255, 255, 255}};
    for (const TexelSize size : {TexelSize::Bits16, TexelSize::Bits32}) {
        const std::vector<u8> packed = n64::packTexels(texels, TextureFormat::Rgba, size);
        const std::vector<Color> unpacked =
            n64::unpackTexels(packed, TextureFormat::Rgba, size, texels.size());
        REQUIRE(unpacked[0] == Color{255, 0, 0, 255});
        REQUIRE(unpacked[1][1] == 255);
        REQUIRE(unpacked[1][3] == 0);
        REQUIRE(unpacked[2] == Color{255, 255, 255, 255});
    }
}

TEST_CASE("texels past the end of the data are transparent", "[n64][unpacking]") {
    const std::vector<Color> unpacked =
        n64::unpackTexels(std::vector<u8>{0xFF, 0xFF}, TextureFormat::Rgba, TexelSize::Bits16, 2);
    REQUIRE(unpacked[1] == Color{0, 0, 0, 0});
}

TEST_CASE("colour indices and palettes unpack as packed", "[n64][unpacking]") {
    const std::vector<u8> indices{1, 15, 7};
    REQUIRE(n64::unpackColorIndices(n64::packColorIndices(indices, TexelSize::Bits4),
                                    TexelSize::Bits4, 3) == indices);
    REQUIRE(n64::unpackColorIndices(indices, TexelSize::Bits8, 3) == indices);

    const std::vector<Color> colors{{255, 0, 0, 255}, {0, 0, 255, 0}};
    const std::vector<Color> palette =
        n64::unpackPalette(n64::packPalette(colors), TlutMode::Rgba16);
    REQUIRE(palette.size() == 4);
    REQUIRE(palette[0] == Color{255, 0, 0, 255});
    REQUIRE(palette[1] == Color{0, 0, 255, 0});
    REQUIRE(n64::unpackPalette(std::vector<u8>{0x80, 0x40}, TlutMode::IntensityAlpha16) ==
            std::vector<Color>{{0x80, 0x80, 0x80, 0x40}});
}
