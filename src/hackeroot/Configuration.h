#pragma once

#include "z64/Diagnostic.h"
#include "z64/MacroTable.h"
#include "z64/SourceSet.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace hackeroot {

/// The macros a HackerOoT build compiles every file with: the build's own
/// `-D` definitions and everything `include/config.h` defines under them,
/// read with real conditionals. `macros` is a layer over the scraped headers,
/// so asset files and tables read against it see the configured value of a
/// feature flag and fall back to the scrape only for names the configuration
/// never mentions.
///
/// The headers are read against no macros at all and only then layered, since
/// the scrape has seen every include guard in the tree and would skip
/// `config.h` whole. A name the configuration headers do define somewhere, but not under this
/// configuration, is hidden rather than left to the scrape: otherwise the
/// scrape's first-seen arm of that `#if` would answer in its place.
///
/// Like any layer, it must outlive every unit read against it, and the scraped
/// table it sits on must outlive it and not move.
struct Configuration {
    z64::SourceSet sources;
    z64::MacroTable macros;
    std::vector<z64::Diagnostic> diagnostics;
};

[[nodiscard]] Configuration readConfiguration(const z64::MacroTable& scraped,
                                              const std::filesystem::path& repository,
                                              std::span<const std::string> defineArguments);

} // namespace hackeroot
