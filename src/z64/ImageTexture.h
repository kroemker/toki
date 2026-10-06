#pragma once

#include "n64/TileDescriptor.h"
#include "z64/TextureName.h"
#include "z64/Types.h"

#include <optional>
#include <string>
#include <string_view>

namespace z64 {

/// What an image or palette file embedded in an object makes of it: the
/// texels' format and size, and the name a colour-indexed image and its
/// palette share, which is how one is paired with the other.
struct ImageTexture {
    bool isPalette = false;
    n64::TextureFormat format = n64::TextureFormat::Rgba;
    n64::TexelSize size = n64::TexelSize::Bits16;
    u32 width = 0;
    u32 height = 0;
    /// For a colour-indexed image, the palette it uses, and for a palette,
    /// the one it is: a shared palette's name, or else the image's prefix.
    std::string paletteName;
    PaletteHalf paletteHalf = PaletteHalf::Whole;
};

/// The texture an embedded file tagged `tag` makes, `fileName` being the file
/// `resolveTextureInclude` resolved it to, and `width` and `height` the
/// image's. Nothing for a file that is neither an image nor a palette.
[[nodiscard]] std::optional<ImageTexture>
makeImageTexture(std::string_view fileName, std::string_view tag, u32 width, u32 height);

} // namespace z64
