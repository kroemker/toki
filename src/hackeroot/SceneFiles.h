#pragma once

#include "base/Diagnostic.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Types.h"

#include <filesystem>
#include <vector>

namespace hackeroot {

inline constexpr u8 SceneSegment = 0x02;
inline constexpr u8 RoomSegment = 0x03;

/// The C files a scene folder's segments are built from: the scene's, for
/// segment 2, and each room's, for segment 3, rooms in order of their number.
/// The file holding a segment's header comes first (`<scene>_scene.c` or
/// fast64's `<scene>_scene_main.c`), because the game reads the header from the
/// start of the segment, and the rest follow by name.
///
/// This stands in for the spec, which is what decides the files of each
/// segment and their order. It is right for the extracted assets and for
/// fast64's exports, and is temporary until the spec is read.
struct SceneFiles {
    std::vector<std::filesystem::path> scene;
    std::vector<std::vector<std::filesystem::path>> rooms;
    /// The folder's own headers, which the C files include.
    std::vector<std::filesystem::path> headers;
    std::vector<base::Diagnostic> diagnostics;

    /// The scene, then each room, as `AssetImageReader::read` reads them.
    [[nodiscard]] std::vector<std::vector<SegmentFiles>> makeImageFiles() const;

    /// Every file read, headers included, for watching them for changes.
    [[nodiscard]] std::vector<std::filesystem::path> getAllFiles() const;
};

[[nodiscard]] SceneFiles findSceneFiles(const AssetDirectory& asset);

/// The game's own C that holds the display lists every scene is drawn with:
/// the setup display lists (`sSetupDL` in `z_rcp.c`) and the default draw
/// config (`sDefaultDisplayList` in `z_scene_table.c`). Read as segment 0,
/// where the game's code is addressed.
[[nodiscard]] SegmentFiles findSceneCodeFiles(const std::filesystem::path& repository);

} // namespace hackeroot
