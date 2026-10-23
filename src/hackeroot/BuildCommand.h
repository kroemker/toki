#pragma once

#include "hackeroot/Types.h"
#include "makeparser/Makefile.h"

#include <filesystem>
#include <string>
#include <vector>

namespace hackeroot {

/// The command that builds the checkout at `root`: `make` inside WSL, for the
/// Makefile's default goal, the ROM, with `settings` as make's command line,
/// exactly as the configuration layer reads them. A checkout on a WSL share
/// runs in its own distribution; any other path runs in the default one, which
/// translates the Windows path itself. `make` is executed directly rather than
/// through a shell, so no setting is ever expanded or split. The first element
/// is the program.
[[nodiscard]] std::vector<std::string> makeBuildCommand(const std::filesystem::path& root,
                                                        const makeparser::MakeOverrides& settings,
                                                        u32 jobs);

} // namespace hackeroot
