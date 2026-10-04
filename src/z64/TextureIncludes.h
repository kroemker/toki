#pragma once

#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace z64 {

/// The tags `resolveTextureInclude` gives the files it resolves to, saying what
/// the build makes of each.
inline constexpr std::string_view ImageTag = "image";
inline constexpr std::string_view PaletteTag = "palette";
inline constexpr std::string_view JpegTag = "jpeg";
inline constexpr std::string_view BinaryTag = "binary";

/// The decomp's rule for an include that names no file: extracted assets
/// include data the build generates, spelled as the file it will write, and
/// that file never exists in the checkout. Such an include resolves to the
/// file the build generates it from, embedded and tagged:
///
/// - `X.rgba16.inc.c` is the image `X.rgba16.png`, tagged `ImageTag`. A
///   paletted image's name may carry more suffixes (`X.ci8.tlut_Y.png`), so
///   the include's spelling is only a prefix of it.
/// - `Y.tlut.rgba16.inc.c` is a palette, tagged `PaletteTag`: the palette of
///   the images naming `tlut_Y`, or of the colour-indexed `Y.ci8.png`. It
///   resolves to one such image. `Y.tlut.rgba16.u32.inc.c` is the palette of
///   images naming `tlut_Y_u32`.
/// - `X.jpg.inc.c` is `X.jpg`, tagged `JpegTag`, and `X.bin.inc.c` is
///   `X.bin`, tagged `BinaryTag`.
///
/// Meant as a `PreprocessOptions::resolveMissingInclude`.
[[nodiscard]] std::optional<cparser::ResolvedInclude>
resolveTextureInclude(const std::filesystem::path& candidate, cparser::DirectoryCache& directories);

} // namespace z64
