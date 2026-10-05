#pragma once

#include "app/scene/Types.h"
#include "base/Diagnostic.h"
#include "hackeroot/SceneFiles.h"
#include "z64/MemoryImage.h"

#include <array>
#include <vector>

namespace toki::scene {

/// An `ActorEntry`: an actor placed in a room, or where the player enters.
/// Rotations are in binary angles, a full turn being 0x10000.
struct ActorPlacement {
    s16 id = 0;
    std::array<s16, 3> position{};
    std::array<s16, 3> rotation{};
    s16 params = 0;
};

/// One of a scene's `EnvLightSettings`: the ambient colour, two directional
/// lights, and fog, for one time of day or one area.
struct LightSetting {
    std::array<u8, 3> ambientColor{};
    std::array<s8, 3> light1Direction{};
    std::array<u8, 3> light1Color{};
    std::array<s8, 3> light2Direction{};
    std::array<u8, 3> light2Color{};
    std::array<u8, 3> fogColor{};
    s16 fogNear = 0;
    s16 zFar = 0;
};

/// How the game picks a scene's current light setting: by the time of day,
/// four settings to a day starting at dawn, or one setting at a time.
enum class LightMode : u8 {
    Time,
    Settings,
};

/// What a scene's header says, read from the scene's bytes in the memory
/// image the way `Scene_ExecuteCommands` reads them: from the start of
/// segment 2, command by command until `SCENE_CMD_END`.
struct SceneHeader {
    usize roomCount = 0;
    u8 skyboxId = 0;
    LightMode lightMode = LightMode::Time;
    std::vector<ActorPlacement> playerEntries;
    std::vector<LightSetting> lightSettings;
    std::vector<base::Diagnostic> diagnostics;
};

enum class RoomShapeType : u8 {
    Normal,
    Image,
    Cullable,
};

/// The opaque and translucent display list of one part of a room, as
/// segmented addresses. Zero where the part has none.
struct RoomDisplayLists {
    u32 opaque = 0;
    u32 translucent = 0;
};

/// What a room's header says, read from the start of segment 3.
struct RoomHeader {
    RoomShapeType shapeType = RoomShapeType::Normal;
    std::vector<RoomDisplayLists> displayLists;
    std::vector<ActorPlacement> actors;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads the main header of the scene in `image`'s segment 2. Alternate
/// headers, for other ages and times of day, are not read yet.
[[nodiscard]] SceneHeader readSceneHeader(const z64::MemoryImage& image);

/// Reads the main header of the room in `image`'s segment 3.
[[nodiscard]] RoomHeader readRoomHeader(const z64::MemoryImage& image);

} // namespace toki::scene
