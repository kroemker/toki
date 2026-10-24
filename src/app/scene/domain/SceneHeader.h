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

    friend bool operator==(const ActorPlacement&, const ActorPlacement&) = default;
};

/// A `TransitionActorEntry`: an actor between two rooms, a door or a loading
/// plane, with the room on its front and on its back side.
struct TransitionActorPlacement {
    std::array<s8, 2> rooms{};
    s16 id = 0;
    std::array<s16, 3> position{};
    s16 rotationY = 0;
    s16 params = 0;

    friend bool operator==(const TransitionActorPlacement&,
                           const TransitionActorPlacement&) = default;
};

/// One of a scene's `EnvLightSettings`: the ambient colour, two directional
/// lights, and fog, for one time of day or one area. `blendRate` is the six
/// bits the fog near value shares its half with, how fast the game blends
/// into the setting, in steps of four.
struct LightSetting {
    std::array<u8, 3> ambientColor{};
    std::array<s8, 3> light1Direction{};
    std::array<u8, 3> light1Color{};
    std::array<s8, 3> light2Direction{};
    std::array<u8, 3> light2Color{};
    std::array<u8, 3> fogColor{};
    s16 fogNear = 0;
    u8 blendRate = 0;
    s16 zFar = 0;

    friend bool operator==(const LightSetting&, const LightSetting&) = default;
};

/// One of a scene's `Path`s: the points an actor that follows it walks, flies
/// or swims through, in order.
struct ScenePath {
    std::vector<std::array<s16, 3>> points;

    friend bool operator==(const ScenePath&, const ScenePath&) = default;
};

/// How the game picks a scene's current light setting: by the time of day,
/// four settings to a day starting at dawn, or one setting at a time.
enum class LightMode : u8 {
    Time,
    Settings,
};

/// A `SceneCmd`'s ID, as the game's command table orders them. HackerOoT's
/// own commands come after these, and their IDs shift with its feature
/// switches, so none of them is here.
enum class CommandCode : u8 {
    PlayerEntryList = 0x00,
    ActorList = 0x01,
    CollisionHeader = 0x03,
    RoomList = 0x04,
    WindSettings = 0x05,
    SpecialFiles = 0x07,
    RoomBehavior = 0x08,
    RoomShape = 0x0A,
    ObjectList = 0x0B,
    PathList = 0x0D,
    TransitionActorList = 0x0E,
    LightSettingsList = 0x0F,
    TimeSettings = 0x10,
    SkyboxSettings = 0x11,
    SkyboxDisables = 0x12,
    ExitList = 0x13,
    End = 0x14,
    SoundSettings = 0x15,
    EchoSettings = 0x16,
    AlternateHeaderList = 0x18,
    MiscSettings = 0x19,
};

/// One `SceneCmd` as the game reads it: its ID, the byte after it, and the
/// word that ends it.
struct HeaderCommand {
    u8 code = 0;
    u8 data1 = 0;
    u32 data2 = 0;

    friend bool operator==(const HeaderCommand&, const HeaderCommand&) = default;
};

/// What a scene's header says, read from the scene's bytes in the memory
/// image the way `Scene_ExecuteCommands` reads them: command by command until
/// `SCENE_CMD_END`.
struct SceneHeader {
    /// Where the header is, as a segmented address.
    u32 address = 0;
    /// Every command before `SCENE_CMD_END`, in order.
    std::vector<HeaderCommand> commands;
    usize roomCount = 0;
    u8 skyboxId = 0;
    LightMode lightMode = LightMode::Time;
    /// The object the game loads into segment 5 for the whole scene, such as
    /// `gameplay_dangeon_keep`, from `SCENE_CMD_SPECIAL_FILES`: zero for none.
    s16 keepObjectId = 0;
    std::vector<ActorPlacement> playerEntries;
    std::vector<TransitionActorPlacement> transitionActors;
    std::vector<LightSetting> lightSettings;
    /// The paths of `SCENE_CMD_PATH_LIST`, which actors name by index. The
    /// list holds no count; it is as long as the array laid out at its address.
    std::vector<ScenePath> paths;
    /// The collision header of `SCENE_CMD_COL_HEADER`, as a segmented address:
    /// zero for none.
    u32 collisionHeader = 0;
    /// The entrances of `SCENE_CMD_EXIT_LIST`, which a surface's exit index
    /// names counting from one. The list holds no count; it is as long as the
    /// array laid out at its address.
    std::vector<u16> exits;
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
    /// As for a scene: where the header is, and its commands.
    u32 address = 0;
    std::vector<HeaderCommand> commands;
    RoomShapeType shapeType = RoomShapeType::Normal;
    std::vector<RoomDisplayLists> displayLists;
    std::vector<ActorPlacement> actors;
    /// The objects the game loads for the room's actors, by ID, from
    /// `SCENE_CMD_OBJECT_LIST`.
    std::vector<s16> objects;
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
