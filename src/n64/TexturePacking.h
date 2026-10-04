#pragma once

#include "n64/Color.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <span>
#include <vector>

namespace n64 {

/// The greyscale an intensity format stores for a colour: Rec. 709 weights in
/// 1.8 fixed point, rounded, as n64texconv computes it.
[[nodiscard]] u8 makeGreyscale(const Color& color);

/// Packs RGBA texels into an N64 texture format the way the decomp's build
/// does with n64texconv: each channel truncated to the format's bits,
/// intensity formats from `makeGreyscale`, one-bit alpha set for any alpha
/// above zero, and 4-bit texels high nibble first. `format` must not be
/// colour-indexed; `packColorIndices` packs those.
[[nodiscard]] std::vector<u8> packTexels(std::span<const Color> texels, TextureFormat format,
                                         TexelSize size);

/// Packs palette indices as CI4 (two to a byte, high nibble first) or CI8.
[[nodiscard]] std::vector<u8> packColorIndices(std::span<const u8> indices, TexelSize size);

/// Packs a palette as RGBA16 entries, padded with zeros to a multiple of
/// eight bytes, as the build writes palettes.
[[nodiscard]] std::vector<u8> packPalette(std::span<const Color> colors);

} // namespace n64
