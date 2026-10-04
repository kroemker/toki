#pragma once

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/SourceSet.h"

#include <expected>
#include <vector>

namespace hackeroot {

class Workspace;

/// The checkout's own `include/ultra64/gbi.h` as the build compiles it, as a
/// macro layer over the workspace's macros: what `z64::DisplayListEncoder`
/// expands display lists against. HackerOoT's own `gs*` additions come with it.
///
/// The header is read against the configuration alone, so every
/// `#ifdef F3DEX_GBI_2` is answered from the Makefile's `GBI_DEFINES` and the
/// headers it includes are read too, rather than skipped for the include
/// guards the scrape has seen. A name the gbi headers define somewhere but not
/// under this configuration is hidden, `gbi.f3dex3.h`'s included: otherwise
/// the scrape's first-seen definition would answer for it, and a command the
/// build would reject would encode as something else.
///
/// The layer sits on the workspace's macros, so the workspace must outlive it
/// and not move.
struct Gbi {
    cparser::SourceSet sources;
    cparser::MacroTable macros;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads the workspace's checkout's gbi. Fails for a checkout that enables
/// F3DEX3, which the renderer does not draw, and for a `gbi.h` that defines no
/// `gs*` macros under the build's configuration.
[[nodiscard]] std::expected<Gbi, base::Diagnostic> readGbi(const Workspace& workspace);

} // namespace hackeroot
