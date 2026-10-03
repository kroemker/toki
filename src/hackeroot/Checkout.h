#pragma once

#include "base/Diagnostic.h"

#include <expected>
#include <filesystem>

namespace hackeroot {

/// Finds the HackerOoT checkout `path` lies in: `path` itself, or the nearest
/// directory above it holding a `Makefile`, `include/config.h` and
/// `include/tables/scene_table.h`, so choosing any folder inside a checkout is
/// enough. The error names what `path` itself is missing.
[[nodiscard]] std::expected<std::filesystem::path, base::Diagnostic>
findCheckoutRoot(const std::filesystem::path& path);

} // namespace hackeroot
