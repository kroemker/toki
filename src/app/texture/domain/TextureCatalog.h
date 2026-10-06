#pragma once

#include "app/texture/Types.h"
#include "n64/Color.h"
#include "z64/ImageTexture.h"
#include "z64/MemoryImage.h"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::texture {

/// A palette a colour-indexed texture is drawn with, and the image it is in.
struct PaletteSource {
    std::string name;
    const z64::MemoryImage* image = nullptr;
    u32 address = 0;
    usize size = 0;
};

/// One texture of an image, as its embedded file made it.
struct TextureEntry {
    std::string name;
    u32 address = 0;
    usize size = 0;
    z64::ImageTexture texture;
    /// For a colour-indexed texture, the palette with its palette name.
    std::optional<PaletteSource> palette;
};

/// A texture's texels in RGBA, row after row from the top.
struct DecodedTexture {
    u32 width = 0;
    u32 height = 0;
    std::vector<n64::Color> pixels;
    /// What could not be decoded as the game would, such as a palette that
    /// was not found, in which case the indices show as grey.
    std::optional<std::string> note;
};

/// Every texture `image` holds, by address. A colour-indexed one is paired
/// with the palette sharing its palette name, looked for in `image` and then
/// in `paletteImages` in order, as a room's textures may use its scene's.
[[nodiscard]] std::vector<TextureEntry>
listTextures(const z64::MemoryImage& image,
             std::span<const z64::MemoryImage* const> paletteImages = {});

/// Decodes `entry` out of `image` as the RDP reads its format. Fails naming
/// the texture when its bytes or its size are unknown.
[[nodiscard]] std::expected<DecodedTexture, std::string>
decodeTexture(const z64::MemoryImage& image, const TextureEntry& entry);

/// The texture's pixels as RGBA bytes, each texel `scale` pixels square, so a
/// small texture can be shown large without being blurred.
[[nodiscard]] std::vector<u8> makeScaledPixels(const DecodedTexture& texture, u32 scale);

/// How a format is written in the decomp's file names: `rgba16`, `ci8`.
[[nodiscard]] std::string getFormatName(const z64::ImageTexture& texture);

} // namespace toki::texture
