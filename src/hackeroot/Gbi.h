#pragma once

#include "base/Diagnostic.h"
#include "hackeroot/Prelude.h"

#include <expected>
#include <optional>

namespace hackeroot {

class Workspace;

/// Why the checkout cannot be drawn at all: it enables F3DEX3, whose display
/// lists the renderer, which runs F3DEX2, would draw wrong. Empty for a
/// checkout that builds for F3DEX2.
[[nodiscard]] std::optional<base::Diagnostic> findUnsupportedMicrocode(const Workspace& workspace);

/// The checkout's own `include/ultra64/gbi.h` as the build compiles it: a
/// prelude of that one header, which is what `z64::DisplayListEncoder` expands
/// display lists against. HackerOoT's own `gs*` additions come with it, and
/// `gbi.f3dex3.h`'s names are hidden with those of the arms the build does not
/// take, so a command the build would reject is reported rather than encoded.
///
/// Fails for a checkout that enables F3DEX3, which the renderer does not
/// draw, and for a `gbi.h` that defines no `gs*` macros under the build's
/// configuration.
[[nodiscard]] std::expected<Prelude, base::Diagnostic> readGbi(const Workspace& workspace);

} // namespace hackeroot
