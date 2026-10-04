#pragma once

#include "n64/TileDescriptor.h"
#include "z64/Types.h"

#include <optional>
#include <string>
#include <string_view>

namespace z64 {

/// Which half of a 256-colour palette a split CI8 image uses.
enum class PaletteHalf : u8 {
    Whole,
    Low,
    High,
};

/// What a texture file's name tells the build about converting it, read the
/// way the build's `build_from_png` reads it. From the end, a name is
/// `<prefix>.<format>[.split_lo|.split_hi][.tlut_<name>[_u32|_u64]][.u32|.u64].png`:
/// `gHylianOldManEyeHalfTex.ci8.tlut_gHylianOldManTLUT_u32.u32.png` is CI8 in
/// u32 elements whose palette is the shared `gHylianOldManTLUT`, in u32
/// elements too.
struct TextureName {
    /// The name up to the format: `gHylianOldManEyeHalfTex`.
    std::string prefix;
    n64::TextureFormat format = n64::TextureFormat::Rgba;
    n64::TexelSize size = n64::TexelSize::Bits16;
    /// Bytes per element of the array the texture fills.
    usize elementSize = 8;
    /// The palette a colour-indexed image shares with others, if any.
    std::optional<std::string> sharedPalette;
    usize paletteElementSize = 8;
    PaletteHalf paletteHalf = PaletteHalf::Whole;
};

/// Reads a texture file name, `.png` included. Nothing for a name that is not
/// one, or that the build would refuse, such as a split palette on anything but
/// a CI8 image sharing its palette.
[[nodiscard]] std::optional<TextureName> parseTextureName(std::string_view fileName);

} // namespace z64
