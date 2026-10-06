#pragma once

#include "base/Diagnostic.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/Types.h"

#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

namespace hackeroot {

class Workspace;

/// An actor's overlay folder, `src/overlays/actors/ovl_<name>`, as the actor
/// table names the actor: the file named after it, `z_<name in lower case>.c`,
/// which holds its profile, and the folder's headers.
struct OverlayFiles {
    std::filesystem::path directory;
    std::filesystem::path main;
    std::vector<std::filesystem::path> headers;

    /// Every file read, headers included, for watching them for changes.
    [[nodiscard]] std::vector<std::filesystem::path> getAllFiles() const;
};

/// The overlay of the actor the actor table calls `name` (`En_Ani`). An actor
/// with no folder, such as one the game code defines itself, is an error.
[[nodiscard]] std::expected<OverlayFiles, base::Diagnostic>
findOverlayFiles(const std::filesystem::path& repository, std::string_view name);

/// Preprocesses an overlay's C against the workspace's build configuration.
/// The overlay's own headers and the asset headers it includes are read, so
/// their declarations say which object holds each symbol it names; the
/// game's headers are not.
[[nodiscard]] std::expected<cparser::TranslationUnit, base::Diagnostic>
preprocessOverlay(const Workspace& workspace, const OverlayFiles& overlay);

} // namespace hackeroot
