#include "app/texture/domain/TextureCatalog.h"

#include "base/Types.h"
#include "n64/Color.h"
#include "n64/TexturePacking.h"
#include "n64/TileDescriptor.h"
#include "z64/ImageTexture.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <optional>
#include <string>
#include <vector>

using base::u32;
using base::u8;
using base::usize;

namespace {

constexpr u8 Segment = 0x06;

class ImageBuilder {
public:
    void add(const std::string& name, const std::vector<u8>& bytes,
             const std::optional<z64::ImageTexture>& texture) {
        image.symbols.emplace(name, z64::ImageSymbol{.segment = Segment,
                                                     .offset = data.size(),
                                                     .size = bytes.size(),
                                                     .type = "u64",
                                                     .isArray = true,
                                                     .texture = texture});
        data.insert(data.end(), bytes.begin(), bytes.end());
        data.resize((data.size() + 7) / 8 * 8, 0);
    }

    [[nodiscard]] z64::MemoryImage makeImage() const {
        z64::MemoryImage made = image;
        made.segments.push_back(z64::ImageSegment{.number = Segment, .bytes = data});
        return made;
    }

private:
    z64::MemoryImage image;
    std::vector<u8> data;
};

z64::ImageTexture makeTexture(const n64::TextureFormat format, const n64::TexelSize size,
                              const u32 width, const u32 height,
                              const std::string& paletteName = {}) {
    return z64::ImageTexture{.format = format,
                             .size = size,
                             .width = width,
                             .height = height,
                             .paletteName = paletteName};
}

z64::ImageTexture makePalette(const std::string& paletteName) {
    return z64::ImageTexture{.isPalette = true, .paletteName = paletteName};
}

constexpr std::array<n64::Color, 2> Colors{{{255, 0, 0, 255}, {0, 0, 255, 255}}};

} // namespace

TEST_CASE("an image's textures are listed by address, paired with their palettes",
          "[texture][catalog]") {
    ImageBuilder builder;
    builder.add("gRedTex",
                n64::packTexels(Colors, n64::TextureFormat::Rgba, n64::TexelSize::Bits16),
                makeTexture(n64::TextureFormat::Rgba, n64::TexelSize::Bits16, 2, 1));
    builder.add("gCrateTLUT", n64::packPalette(Colors), makePalette("gCrateTex"));
    builder.add(
        "gCrateTex", n64::packColorIndices(std::vector<u8>{1, 0}, n64::TexelSize::Bits8),
        makeTexture(n64::TextureFormat::ColorIndex, n64::TexelSize::Bits8, 2, 1, "gCrateTex"));
    builder.add("gCrateDL", {0xDF, 0, 0, 0, 0, 0, 0, 0}, std::nullopt);
    const z64::MemoryImage image = builder.makeImage();

    const std::vector<toki::texture::TextureEntry> entries = toki::texture::listTextures(image);

    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].name == "gRedTex");
    REQUIRE(entries[1].name == "gCrateTex");
    REQUIRE(entries[0].address < entries[1].address);
    REQUIRE_FALSE(entries[0].palette.has_value());
    REQUIRE(entries[1].palette.has_value());
    REQUIRE(entries[1].palette->name == "gCrateTLUT");
}

TEST_CASE("a texture decodes through its format and its palette", "[texture][catalog]") {
    ImageBuilder builder;
    builder.add("gRedTex",
                n64::packTexels(Colors, n64::TextureFormat::Rgba, n64::TexelSize::Bits16),
                makeTexture(n64::TextureFormat::Rgba, n64::TexelSize::Bits16, 2, 1));
    builder.add("gCrateTLUT", n64::packPalette(Colors), makePalette("gCrateTex"));
    builder.add(
        "gCrateTex", n64::packColorIndices(std::vector<u8>{1, 0}, n64::TexelSize::Bits8),
        makeTexture(n64::TextureFormat::ColorIndex, n64::TexelSize::Bits8, 2, 1, "gCrateTex"));
    const z64::MemoryImage image = builder.makeImage();
    const std::vector<toki::texture::TextureEntry> entries = toki::texture::listTextures(image);

    const auto red = toki::texture::decodeTexture(image, entries[0]);
    const auto crate = toki::texture::decodeTexture(image, entries[1]);

    REQUIRE(red.has_value());
    REQUIRE(std::ranges::equal(red->pixels, Colors));
    REQUIRE(crate.has_value());
    REQUIRE_FALSE(crate->note.has_value());
    REQUIRE(crate->pixels == std::vector<n64::Color>{Colors[1], Colors[0]});
}

TEST_CASE("a palette is looked for in the other images too", "[texture][catalog]") {
    ImageBuilder sceneBuilder;
    sceneBuilder.add("gSceneTLUT", n64::packPalette(Colors), makePalette("gShared"));
    const z64::MemoryImage scene = sceneBuilder.makeImage();
    ImageBuilder roomBuilder;
    roomBuilder.add(
        "gRoomTex", n64::packColorIndices(std::vector<u8>{0, 1}, n64::TexelSize::Bits4),
        makeTexture(n64::TextureFormat::ColorIndex, n64::TexelSize::Bits4, 2, 1, "gShared"));
    const z64::MemoryImage room = roomBuilder.makeImage();

    const std::array<const z64::MemoryImage*, 1> others{&scene};
    const std::vector<toki::texture::TextureEntry> alone = toki::texture::listTextures(room);
    const std::vector<toki::texture::TextureEntry> paired =
        toki::texture::listTextures(room, others);

    REQUIRE_FALSE(alone[0].palette.has_value());
    const auto grey = toki::texture::decodeTexture(room, alone[0]);
    REQUIRE(grey->note.has_value());
    REQUIRE(grey->pixels[1] == n64::Color{0x11, 0x11, 0x11, 0xFF});
    REQUIRE(paired[0].palette->name == "gSceneTLUT");
    REQUIRE(std::ranges::equal(toki::texture::decodeTexture(room, paired[0])->pixels, Colors));
}

TEST_CASE("a texture of unknown size does not decode", "[texture][catalog]") {
    ImageBuilder builder;
    builder.add("gTex", {0, 0},
                makeTexture(n64::TextureFormat::Rgba, n64::TexelSize::Bits16, 0, 0));
    const z64::MemoryImage image = builder.makeImage();

    const auto decoded =
        toki::texture::decodeTexture(image, toki::texture::listTextures(image).front());

    REQUIRE_FALSE(decoded.has_value());
    REQUIRE(decoded.error().contains("gTex"));
}

TEST_CASE("scaled pixels repeat each texel in a square", "[texture][catalog]") {
    const toki::texture::DecodedTexture texture{
        .width = 2, .height = 1, .pixels = std::vector<n64::Color>(Colors.begin(), Colors.end())};

    const std::vector<u8> scaled = toki::texture::makeScaledPixels(texture, 2);

    REQUIRE(scaled.size() == usize{4} * 2 * 4);
    REQUIRE(scaled[0] == 255);
    REQUIRE(scaled[4] == 255);
    REQUIRE(scaled[(2 * 4) + 2] == 255);
    REQUIRE(scaled[(4 * 4) + 0] == 255);
    REQUIRE(scaled[(6 * 4) + 2] == 255);
}

TEST_CASE("formats are named as the decomp's file names spell them", "[texture][catalog]") {
    REQUIRE(toki::texture::getFormatName(
                makeTexture(n64::TextureFormat::ColorIndex, n64::TexelSize::Bits4, 1, 1)) == "ci4");
    REQUIRE(toki::texture::getFormatName(
                makeTexture(n64::TextureFormat::Rgba, n64::TexelSize::Bits16, 1, 1)) == "rgba16");
}
