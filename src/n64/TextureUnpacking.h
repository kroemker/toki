#pragma once

#include "n64/Color.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <span>
#include <vector>

namespace n64 {

/// An RGBA16 value as the RDP expands it: five bits a channel widened by
/// repeating their top bits, and alpha all or nothing.
[[nodiscard]] Color decodeRgba16(u32 value);

/// An IA16 value: intensity in the high byte, alpha in the low.
[[nodiscard]] Color decodeIntensityAlpha16(u32 value);

/// The texels of a texture as it lies in RDRAM, row after row, read the way
/// the RDP decodes each format: channels widened by repeating their top bits,
/// intensity formats grey with alpha of their own or, for I, the intensity as
/// alpha. `count` texels are read, and ones past the end of `bytes` are
/// transparent black. `format` must not be colour-indexed;
/// `unpackColorIndices` reads those.
[[nodiscard]] std::vector<Color> unpackTexels(std::span<const u8> bytes, TextureFormat format,
                                              TexelSize size, usize count);

/// The palette indices of a CI4 or CI8 texture, high nibble first.
[[nodiscard]] std::vector<u8> unpackColorIndices(std::span<const u8> bytes, TexelSize size,
                                                 usize count);

/// A palette's colours, RGBA16 or IA16 entries as `tlut` says.
[[nodiscard]] std::vector<Color> unpackPalette(std::span<const u8> bytes, TlutMode tlut);

} // namespace n64
