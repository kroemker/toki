#pragma once

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/SourceSet.h"
#include "cparser/TypeTable.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hackeroot {

class Workspace;

/// Headers of the checkout read the way the build compiles them, for what an
/// asset file takes from outside the asset tree: its types and the macros its
/// initializers use. `macros` is a layer over the workspace's macros, and
/// `types` holds every type the headers define, laid out for the N64.
///
/// The headers are read against the configuration alone, so every condition
/// is answered as the build answers it and every header they include is read
/// too, rather than skipped for the include guards the scrape has seen. A name
/// the headers define somewhere but not under this configuration is hidden:
/// otherwise the scrape's first-seen definition would answer for it.
///
/// The layer sits on the workspace's macros, so the workspace must outlive it
/// and not move.
struct Prelude {
    cparser::SourceSet sources;
    cparser::MacroTable macros;
    cparser::TypeTable types;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads the headers an asset file includes, as its `#include` spells them,
/// from the build's include paths.
[[nodiscard]] Prelude readPrelude(const Workspace& workspace,
                                  std::span<const std::string> includes);

/// The same, also hiding the names `alsoHidden` headers define, for headers
/// the configuration does not include but the scrape has read.
[[nodiscard]] Prelude readPrelude(const Workspace& workspace, std::span<const std::string> includes,
                                  std::span<const std::string_view> alsoHidden);

} // namespace hackeroot
