#pragma once

#include "base/Diagnostic.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/SourceSet.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace hackeroot {

/// The macros a HackerOoT build compiles every file with: what the MIPS GCC
/// it builds with (`mips-linux-gnu-gcc` 10, `-std=gnu11`, `-march=vr4300`)
/// defines on its own, such as `_LANGUAGE_C`, which `gbi.h` wraps its C half
/// in, and `__GNUC__`, which headers choose GCC's syntax by, then the
/// build's own `-D` definitions, and everything
/// `include/config.h` defines under them,
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
    cparser::SourceSet sources;
    cparser::MacroTable macros;
    std::vector<base::Diagnostic> diagnostics;
};

/// What the build's configuration is read with: the checkout's include paths,
/// and only the headers `config.h` reaches.
[[nodiscard]] cparser::PreprocessOptions
makeConfigurationOptions(const std::filesystem::path& repository);

/// The text the build's configuration is read from: the compiler's own
/// macros, then `defineArguments`, then `#include "config.h"`.
[[nodiscard]] std::string makeConfigurationSource(std::span<const std::string> defineArguments);

[[nodiscard]] Configuration readConfiguration(const cparser::MacroTable& scraped,
                                              const std::filesystem::path& repository,
                                              std::span<const std::string> defineArguments);

} // namespace hackeroot
