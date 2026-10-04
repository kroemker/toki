#pragma once

#include "n64/Color.h"
#include "z64/Types.h"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace z64 {

/// A decoded PNG. A palette-based PNG keeps its palette, transparency
/// included, and its colour indices, because a colour-indexed texture takes
/// both as they are; its texels are the palette looked up.
struct PngImage {
    u32 width = 0;
    u32 height = 0;
    std::vector<n64::Color> texels;
    std::vector<n64::Color> palette;
    std::vector<u8> indices;
};

/// Decodes PNG file contents. Fails with libspng's reason.
[[nodiscard]] std::expected<PngImage, std::string> decodePng(std::span<const u8> bytes);

/// Only the palette of a palette-based PNG, read without decoding the image.
/// Empty for a PNG without one.
[[nodiscard]] std::expected<std::vector<n64::Color>, std::string>
decodePngPalette(std::span<const u8> bytes);

} // namespace z64
