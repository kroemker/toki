#pragma once

#include "cparser/DirectoryCache.h"
#include "cparser/IncludeResolver.h"

#include <filesystem>
#include <optional>

namespace z64 {

/// The decomp's rule for an include that names no file: extracted assets
/// include texture data as `X.rgba16.inc.c`, which the build generates from
/// `X.rgba16.png`, so the source names a file that never exists in the
/// checkout. That include resolves to the image, embedded, tagged with the N64
/// format its name spells. A paletted texture's image also names the palette it
/// was extracted against (`X.ci8.tlut_Y.png`), so the include's spelling is
/// only a prefix of it. `X.bin.inc.c` resolves to `X.bin`.
///
/// Meant as a `PreprocessOptions::resolveMissingInclude`.
[[nodiscard]] std::optional<cparser::ResolvedInclude>
resolveTextureInclude(const std::filesystem::path& candidate, cparser::DirectoryCache& directories);

} // namespace z64
