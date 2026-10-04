#pragma once

#include "hackeroot/Types.h"
#include "makeparser/Makefile.h"

#include <filesystem>
#include <string>
#include <vector>

namespace hackeroot {

enum class BuildTarget : u8 {
    Build,
    Run,
};

/// The command that builds the checkout at `root`: `make` inside WSL, with
/// `settings` as make's command line, exactly as the configuration layer reads
/// them, and `run` appended for `BuildTarget::Run`. A checkout on a WSL share
/// runs in its own distribution; any other path runs in the default one, which
/// translates the Windows path itself. `make` is executed directly rather than
/// through a shell, so no setting is ever expanded or split. The first element
/// is the program.
[[nodiscard]] std::vector<std::string> makeBuildCommand(const std::filesystem::path& root,
                                                        const makeparser::MakeOverrides& settings,
                                                        BuildTarget target, u32 jobs);

} // namespace hackeroot
