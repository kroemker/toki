#include "n64/TextureMemory.h"

#include "base/Types.h"
#include "n64/Color.h"
#include "n64/Memory.h"
#include "n64/OtherMode.h"
#include "n64/RspResult.h"
#include "n64/TexelFetch.h"
#include "n64/TileDescriptor.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <initializer_list>
#include <utility>
#include <vector>

using base::s32;
using base::u32;
using base::u8;
using n64::Color;
using n64::TexelSize;
using n64::TextureFormat;
using n64::TlutMode;

namespace {

constexpr u32 TextureAddress = 0x2000;
constexpr u32 LoadTile = 7;

struct TileSetup {
    TextureFormat format = TextureFormat::Rgba;
    TexelSize size = TexelSize::Bits16;
    u32 line = 0;
    u32 address = 0;
    u32 palette = 0;
    u32 axisS = 0;
    u32 axisT = 0;
};

n64::RdpCommand makeCommand(const u32 word0, const u32 word1) {
    return n64::RdpCommand{.word0 = word0, .word1 = word1, .address = 0x10'0000};
}

n64::RdpCommand setTextureImage(const TextureFormat format, const TexelSize size, const u32 width,
                                const u32 address) {
    return makeCommand(0xFD00'0000U | (u32{static_cast<u8>(format)} << 21) |
                           (u32{static_cast<u8>(size)} << 19) | (width - 1),
                       address);
}

n64::RdpCommand setTile(const u32 tile, const TileSetup& setup) {
    return makeCommand(0xF500'0000U | (u32{static_cast<u8>(setup.format)} << 21) |
                           (u32{static_cast<u8>(setup.size)} << 19) | (setup.line << 9) |
                           setup.address,
                       (tile << 24) | (setup.palette << 20) | (setup.axisT << 10) | setup.axisS);
}

n64::RdpCommand setTileSize(const u32 tile, const u32 width, const u32 height) {
    return makeCommand(0xF200'0000U,
                       (tile << 24) | (((width - 1) << 2) << 12) | ((height - 1) << 2));
}

n64::RdpCommand loadTile(const u32 width, const u32 height) {
    return makeCommand(0xF400'0000U,
                       (LoadTile << 24) | (((width - 1) << 2) << 12) | ((height - 1) << 2));
}

n64::RdpCommand loadBlock(const u32 texels, const u32 dxt) {
    return makeCommand(0xF300'0000U, (LoadTile << 24) | ((texels - 1) << 12) | dxt);
}

n64::RdpCommand loadTlut(const u32 entries) {
    return makeCommand(0xF000'0000U, (LoadTile << 24) | ((entries - 1) << 14));
}

class Fixture {
public:
    explicit Fixture(std::vector<u8> source) : source(std::move(source)) {
        REQUIRE(memory.map(TextureAddress, this->source));
    }

    void apply(const std::initializer_list<n64::RdpCommand> commands) {
        for (const n64::RdpCommand& command : commands) {
            REQUIRE_FALSE(textureMemory.apply(command, memory).has_value());
        }
    }

    [[nodiscard]] Color fetch(const u32 tile, const n64::Texel texel,
                              const TlutMode tlut = TlutMode::None) const {
        return n64::fetchTexel(textureMemory.getBytes(), textureMemory.getTile(tile), texel, tlut);
    }

    std::vector<u8> source;
    n64::Memory memory;
    n64::TextureMemory textureMemory;
};

std::vector<u8> makeIntensityAlphaTexture(const u32 texels) {
    std::vector<u8> bytes;
    for (u32 k = 0; k < texels; ++k) {
        bytes.push_back(static_cast<u8>((k * 16) + 1));
        bytes.push_back(static_cast<u8>((k * 16) + 2));
    }
    return bytes;
}

void requireIntensityAlphaTexels(const Fixture& fixture) {
    for (s32 y = 0; y < 2; ++y) {
        for (s32 x = 0; x < 4; ++x) {
            const auto k = static_cast<u32>((y * 4) + x);
            const auto intensity = static_cast<u8>((k * 16) + 1);
            REQUIRE(fixture.fetch(0, {x, y}) ==
                    Color{intensity, intensity, intensity, static_cast<u8>((k * 16) + 2)});
        }
    }
}

const TileSetup IntensityAlpha16{
    .format = TextureFormat::IntensityAlpha, .size = TexelSize::Bits16, .line = 1};

} // namespace

TEST_CASE("a block load swaps the halves of every odd row, and reading swaps them back",
          "[n64][tmem]") {
    Fixture fixture(makeIntensityAlphaTexture(8));
    fixture.apply({
        setTextureImage(TextureFormat::IntensityAlpha, TexelSize::Bits16, 4, TextureAddress),
        setTile(LoadTile, {.format = TextureFormat::IntensityAlpha, .size = TexelSize::Bits16}),
        loadBlock(8, 0x800),
        setTile(0, IntensityAlpha16),
        setTileSize(0, 4, 2),
    });

    REQUIRE(fixture.textureMemory.getBytes()[0] == fixture.source[0]);
    REQUIRE(fixture.textureMemory.getBytes()[8] == fixture.source[12]);
    requireIntensityAlphaTexels(fixture);
}

TEST_CASE("a tile load lays rows out by the tile's line", "[n64][tmem]") {
    Fixture fixture(makeIntensityAlphaTexture(8));
    fixture.apply({
        setTextureImage(TextureFormat::IntensityAlpha, TexelSize::Bits16, 4, TextureAddress),
        setTile(LoadTile, IntensityAlpha16),
        loadTile(4, 2),
        setTile(0, IntensityAlpha16),
        setTileSize(0, 4, 2),
    });

    REQUIRE(fixture.textureMemory.getBytes()[8] == fixture.source[12]);
    requireIntensityAlphaTexels(fixture);
}

TEST_CASE("a paletted texture reads its colours from the loaded palette", "[n64][tmem]") {
    std::vector<u8> source{0x12, 0x3F, 0, 0, 0, 0, 0, 0};
    for (u32 i = 0; i < 16; ++i) {
        const u32 entry = (i << 11) | 1;
        source.push_back(static_cast<u8>(entry >> 8));
        source.push_back(static_cast<u8>(entry));
    }
    Fixture fixture(std::move(source));
    fixture.apply({
        setTextureImage(TextureFormat::Rgba, TexelSize::Bits16, 1, TextureAddress + 8),
        setTile(LoadTile, {.address = 256}),
        loadTlut(16),
        setTextureImage(TextureFormat::ColorIndex, TexelSize::Bits16, 1, TextureAddress),
        setTile(LoadTile, {.format = TextureFormat::ColorIndex}),
        loadBlock(4, 0),
        setTile(0, {.format = TextureFormat::ColorIndex, .size = TexelSize::Bits4, .line = 1}),
    });

    REQUIRE(fixture.textureMemory.getBytes()[0x800 + 8 + 4] == 0x08);
    REQUIRE(fixture.fetch(0, {0, 0}, TlutMode::Rgba16) == Color{8, 0, 0, 255});
    REQUIRE(fixture.fetch(0, {3, 0}, TlutMode::Rgba16) == Color{123, 0, 0, 255});
    REQUIRE(fixture.fetch(0, {3, 0}) == Color{0x0F, 0x0F, 0x0F, 0x0F});
}

TEST_CASE("32-bit RGBA is split between the two halves of texture memory", "[n64][tmem]") {
    Fixture fixture({1, 2, 3, 4, 5, 6, 7, 8});
    const TileSetup rgba32{.format = TextureFormat::Rgba, .size = TexelSize::Bits32, .line = 1};
    fixture.apply({
        setTextureImage(TextureFormat::Rgba, TexelSize::Bits32, 2, TextureAddress),
        setTile(LoadTile, rgba32),
        loadTile(2, 1),
        setTile(0, rgba32),
    });

    REQUIRE(fixture.textureMemory.getBytes()[2] == 5);
    REQUIRE(fixture.textureMemory.getBytes()[0x800] == 3);
    REQUIRE(fixture.fetch(0, {0, 0}) == Color{1, 2, 3, 4});
    REQUIRE(fixture.fetch(0, {1, 0}) == Color{5, 6, 7, 8});
}

TEST_CASE("4-bit and 8-bit intensity formats expand to eight bits", "[n64][tmem]") {
    Fixture fixture({0x9E, 0xA5, 0, 0, 0, 0, 0, 0});
    fixture.apply({
        setTextureImage(TextureFormat::Intensity, TexelSize::Bits16, 4, TextureAddress),
        setTile(LoadTile, {}),
        loadBlock(4, 0),
        setTile(0, {.format = TextureFormat::Intensity, .size = TexelSize::Bits4, .line = 1}),
        setTile(1, {.format = TextureFormat::IntensityAlpha, .size = TexelSize::Bits4, .line = 1}),
        setTile(2, {.format = TextureFormat::IntensityAlpha, .size = TexelSize::Bits8, .line = 1}),
    });

    REQUIRE(fixture.fetch(0, {0, 0}) == Color{0x99, 0x99, 0x99, 0x99});
    REQUIRE(fixture.fetch(1, {0, 0}) == Color{0x92, 0x92, 0x92, 0xFF});
    REQUIRE(fixture.fetch(1, {1, 0}) == Color{0xFF, 0xFF, 0xFF, 0x00});
    REQUIRE(fixture.fetch(2, {1, 0}) == Color{0xAA, 0xAA, 0xAA, 0x55});
}

TEST_CASE("wrapping, mirroring and clamping follow the tile", "[n64][tmem]") {
    n64::TileDescriptor tile;
    tile.axes[0] = {.clamp = false, .mirror = false, .mask = 2, .shift = 0, .low = 0, .high = 12};
    tile.axes[1] = {.clamp = false, .mirror = true, .mask = 2, .shift = 0, .low = 0, .high = 12};

    REQUIRE(n64::wrapTexel(tile, {5, 5}) == n64::Texel{1, 2});
    REQUIRE(n64::wrapTexel(tile, {-1, -1}) == n64::Texel{3, 0});

    tile.axes[0] = {.clamp = true, .mirror = false, .mask = 2, .shift = 0, .low = 0, .high = 8};
    tile.axes[1] = {.clamp = false, .mirror = false, .mask = 0, .shift = 0, .low = 4, .high = 12};

    REQUIRE(n64::wrapTexel(tile, {7, 7}) == n64::Texel{2, 2});
    REQUIRE(n64::wrapTexel(tile, {-3, -3}) == n64::Texel{0, 0});
}

TEST_CASE("a tile's shift and corner move the coordinate", "[n64][tmem]") {
    n64::TileDescriptor tile;
    tile.axes[0] = {.shift = 1, .low = 8};
    tile.axes[1] = {.shift = 15};

    REQUIRE(n64::findTileTexel(tile, {9.5F, 3.25F}) == n64::Texel{2, 6});
    REQUIRE(n64::findTileTexel(tile, {1.0F, -0.25F}) == n64::Texel{-2, -1});
}

TEST_CASE("a load outside mapped memory is reported", "[n64][tmem]") {
    const n64::Memory memory;
    n64::TextureMemory textureMemory;
    REQUIRE_FALSE(
        textureMemory
            .apply(setTextureImage(TextureFormat::Rgba, TexelSize::Bits16, 4, 0x8000), memory)
            .has_value());

    const auto diagnostic = textureMemory.apply(loadBlock(4, 0), memory);

    REQUIRE(diagnostic.has_value());
    REQUIRE(diagnostic->message.contains("G_LOADBLOCK"));
}

TEST_CASE("bilinear filtering takes three texels by the fraction's half", "[n64][tmem]") {
    Fixture fixture(makeIntensityAlphaTexture(8));
    fixture.apply({
        setTextureImage(TextureFormat::IntensityAlpha, TexelSize::Bits16, 4, TextureAddress),
        setTile(LoadTile, IntensityAlpha16),
        loadTile(4, 2),
        setTile(0, IntensityAlpha16),
        setTileSize(0, 4, 2),
    });
    const auto sample = [&](const float s, const float t, const n64::TextureFilter filter) {
        return n64::sampleTexture(fixture.textureMemory.getBytes(),
                                  fixture.textureMemory.getTile(0), {s, t}, TlutMode::None, filter);
    };

    REQUIRE(sample(1.25F, 0.0F, n64::TextureFilter::Point) == fixture.fetch(0, {1, 0}));
    REQUIRE(sample(1.25F, 0.0F, n64::TextureFilter::Bilinear)[0] == 21);
    REQUIRE(sample(1.75F, 0.75F, n64::TextureFilter::Bilinear)[0] == 77);
    REQUIRE(sample(0.5F, 0.5F, n64::TextureFilter::Average)[0] == 41);
}
