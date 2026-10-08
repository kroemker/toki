#pragma once

#include "hackeroot/AssetImage.h"
#include "hackeroot/Types.h"
#include "z64/MemoryImage.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace hackeroot {

/// The game's own C that holds the display lists rooms and actors are drawn
/// with: the setup display lists (`sSetupDL` in `z_rcp.c`) and the default
/// draw config (`sDefaultDisplayList` in `z_scene_table.c`). Read as segment
/// 0, where the game's code is addressed.
[[nodiscard]] SegmentFiles findGameCodeFiles(const std::filesystem::path& repository);

/// The game code as a shared image, watching its files.
[[nodiscard]] SharedImageFiles makeSharedGameCode(const std::filesystem::path& repository);

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

/// The index of `SETUPDL_44`, which shadows are drawn after.
inline constexpr usize SetupDisplayList44 = 44;

/// Where `sSetupDL[index]` is in an image of `findGameCodeFiles`, if the code
/// could be read and has that many.
[[nodiscard]] std::optional<u32> findSetupDisplayList(const z64::MemoryImage& code, usize index);

/// Where every display list of `sSetupDL` is, by index, in an image of
/// `findGameCodeFiles`; none when the code could not be read.
[[nodiscard]] std::vector<u32> listSetupDisplayLists(const z64::MemoryImage& code);

} // namespace hackeroot
