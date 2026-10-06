#pragma once

#include "hackeroot/AssetImage.h"
#include "hackeroot/Types.h"
#include "z64/MemoryImage.h"

#include <filesystem>
#include <optional>

namespace hackeroot {

/// The game's own C that holds the display lists rooms and actors are drawn
/// with: the setup display lists (`sSetupDL` in `z_rcp.c`) and the default
/// draw config (`sDefaultDisplayList` in `z_scene_table.c`). Read as segment
/// 0, where the game's code is addressed.
[[nodiscard]] SegmentFiles findGameCodeFiles(const std::filesystem::path& repository);

/// What the game draws rooms and most actors after, found in an image of
/// `findGameCodeFiles`: the default draw config, and `sSetupDL[SETUPDL_25]`,
/// which `Gfx_SetupDL_25Opa` and `Gfx_SetupDL_25Xlu` call. Either is missing
/// when the code could not be read.
struct GameSetup {
    std::optional<u32> drawConfig;
    std::optional<u32> setupDisplayList;
};

[[nodiscard]] GameSetup findGameSetup(const z64::MemoryImage& code);

/// The index of `SETUPDL_25` among the setup display lists.
inline constexpr usize SetupDisplayList25 = 25;

} // namespace hackeroot
