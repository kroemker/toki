#pragma once

#include "base/Diagnostic.h"
#include "makeparser/Makefile.h"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace hackeroot {

struct BuildDefines {
    /// The `-D`, `-U` and `-std=` arguments the build hands to `cpp`, in order
    /// and with the shell's quoting removed.
    std::vector<std::string> arguments;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads the preprocessor definitions a HackerOoT build compiles with out of
/// the checkout's own Makefile: `CPP_DEFINES` and `GBI_DEFINES`, as make would
/// expand them with `settings` on its command line. `DEBUG_FEATURES`,
/// `OOT_VERSION` and `PLATFORM_*` come from here and from nowhere else, and
/// the ordinal tables depend on them.
///
/// Two variables are computed in the Makefile by piping through `tr`, which is
/// never run here. `VERSION_MACRO` and `COMPRESSION_TYPE` are derived the same
/// way and passed as command-line overrides, unless `settings` already sets
/// them. This is a hack that mirrors those two lines; it must follow them if
/// they change.
[[nodiscard]] std::expected<BuildDefines, base::Diagnostic>
readBuildDefines(const std::filesystem::path& repository,
                 const makeparser::MakeOverrides& settings);

} // namespace hackeroot
