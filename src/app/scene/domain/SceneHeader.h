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

/// A `TransitionActorEntry`: an actor between two rooms, a door or a loading
/// plane, with the room on its front and on its back side.
struct TransitionActorPlacement {
    std::array<s8, 2> rooms{};
    s16 id = 0;
    std::array<s16, 3> position{};
    s16 rotationY = 0;
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
/// image the way `Scene_ExecuteCommands` reads them: command by command until
/// `SCENE_CMD_END`.
struct SceneHeader {
    usize roomCount = 0;
    u8 skyboxId = 0;
    LightMode lightMode = LightMode::Time;
    std::vector<ActorPlacement> playerEntries;
    std::vector<TransitionActorPlacement> transitionActors;
    std::vector<LightSetting> lightSettings;
    /// The headers of the scene's other layers, from layer 1 on, as segmented
    /// addresses: zero where a layer has none of its own.
    std::vector<u32> alternateHeaders;
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

/// What a room's header says.
struct RoomHeader {
    RoomShapeType shapeType = RoomShapeType::Normal;
    std::vector<RoomDisplayLists> displayLists;
    std::vector<ActorPlacement> actors;
    /// As for a scene: the headers of the room's other layers, from layer 1.
    std::vector<u32> alternateHeaders;
    std::vector<base::Diagnostic> diagnostics;
};

/// Where the game reads a scene's main header: the start of segment 2.
inline constexpr u32 SceneHeaderAddress = u32{hackeroot::SceneSegment} << 24;

/// Where the game reads a room's main header: the start of segment 3.
inline constexpr u32 RoomHeaderAddress = u32{hackeroot::RoomSegment} << 24;

/// Reads the scene header at `address` in `image`. The main header lists the
/// alternate ones; an alternate header lists none.
[[nodiscard]] SceneHeader readSceneHeader(const z64::MemoryImage& image,
                                          u32 address = SceneHeaderAddress);

/// Reads the room header at `address` in `image`.
[[nodiscard]] RoomHeader readRoomHeader(const z64::MemoryImage& image,
                                        u32 address = RoomHeaderAddress);

} // namespace toki::scene
