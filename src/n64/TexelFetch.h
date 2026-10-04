#pragma once

#include "n64/TextureMemory.h"
#include "n64/TileDescriptor.h"
#include "n64/Types.h"

#include <array>
#include <span>

namespace n64 {

using Texel = std::array<s32, 2>;
using Color = std::array<u8, 4>;

/// Where a texture coordinate falls in a tile: shifted by the tile's shift,
/// moved by its upper-left corner, and floored to a whole texel. `coordinate`
/// is in texels, as the RSP produces it.
[[nodiscard]] Texel findTileTexel(const TileDescriptor& tile, std::array<f32, 2> coordinate);

/// Clamps, mirrors and wraps a texel the way the RDP does. An axis with a zero
/// mask clamps whether or not clamping is set, and clamping keeps the texel
/// within the tile's size.
[[nodiscard]] Texel wrapTexel(const TileDescriptor& tile, Texel texel);

/// Reads one texel out of texture memory as RGBA, decoding the tile's format
/// and, when `tlut` says so, looking it up in the palette in the upper half of
/// the memory. Odd rows are read with their 32-bit halves swapped back. Formats
/// the hardware has no decoder for read the way the hardware was observed to
/// read them.
[[nodiscard]] Color fetchTexel(std::span<const u8, TextureMemorySize> memory,
                               const TileDescriptor& tile, Texel texel, TlutMode tlut);

/// `findTileTexel`, `wrapTexel` and `fetchTexel` together: point sampling.
[[nodiscard]] Color sampleTexel(std::span<const u8, TextureMemorySize> memory,
                                const TileDescriptor& tile, std::array<f32, 2> coordinate,
                                TlutMode tlut);

} // namespace n64
