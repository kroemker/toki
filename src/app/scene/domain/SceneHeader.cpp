#include "SceneHeader.h"

#include "app/scene/Types.h"
#include "base/Diagnostic.h"
#include "n64/BigEndian.h"
#include "z64/MemoryImage.h"

#include <array>
#include <format>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize CommandSize = 8;
constexpr usize MaximumCommands = 64;
constexpr usize ActorEntrySize = 0x10;
constexpr usize LightSettingSize = 0x16;
constexpr usize DisplayListsEntrySize = 0x08;
constexpr usize CullableEntrySize = 0x10;
constexpr usize RoomShapeSize = 0x08;
constexpr usize TransitionActorEntrySize = 0x10;
constexpr usize PointerSize = 4;
constexpr usize UnsizedAlternateHeaders = 3;
constexpr u16 FogNearMask = 0x03FF;

enum class CommandCode : u8 {
    PlayerEntryList = 0x00,
    ActorList = 0x01,
    RoomList = 0x04,
    RoomShape = 0x0A,
    TransitionActorList = 0x0E,
    LightSettingsList = 0x0F,
    SkyboxSettings = 0x11,
    End = 0x14,
    AlternateHeaderList = 0x18,
};

struct Command {
    u8 code = 0;
    u8 data1 = 0;
    u32 data2 = 0;
};

[[nodiscard]] bool isCode(const Command& command, const CommandCode code) {
    return command.code == static_cast<u8>(code);
}

[[nodiscard]] std::array<s16, 3> readVector(const std::span<const u8> bytes, const usize offset) {
    return {n64::readS16(bytes, offset), n64::readS16(bytes, offset + 2),
            n64::readS16(bytes, offset + 4)};
}

[[nodiscard]] std::array<u8, 3> readColor(const std::span<const u8> bytes, const usize offset) {
    return {bytes[offset], bytes[offset + 1], bytes[offset + 2]};
}

[[nodiscard]] std::array<s8, 3> readDirection(const std::span<const u8> bytes, const usize offset) {
    return {static_cast<s8>(bytes[offset]), static_cast<s8>(bytes[offset + 1]),
            static_cast<s8>(bytes[offset + 2])};
}

class HeaderReader {
public:
    explicit HeaderReader(const z64::MemoryImage& image) : image(image) {}

    [[nodiscard]] std::vector<Command> readCommands(u32 start);

    [[nodiscard]] std::vector<ActorPlacement> readActors(const Command& command);

    [[nodiscard]] std::vector<TransitionActorPlacement>
    readTransitionActors(const Command& command);

    [[nodiscard]] std::vector<u32> readAlternateHeaders(const Command& command);

    [[nodiscard]] std::vector<LightSetting> readLightSettings(const Command& command);

    [[nodiscard]] RoomHeader readRoomShape(u32 address);

    [[nodiscard]] std::vector<base::Diagnostic> takeDiagnostics() {
        return std::move(diagnostics);
    }

private:
    [[nodiscard]] std::optional<std::span<const u8>> findArray(u32 address, usize count,
                                                               usize elementSize, const char* what);

    [[nodiscard]] std::vector<RoomDisplayLists> readEntries(u32 address, usize count, usize stride,
                                                            usize listsOffset);

    const z64::MemoryImage& image;
    std::vector<base::Diagnostic> diagnostics;
};

std::vector<Command> HeaderReader::readCommands(const u32 start) {
    std::vector<Command> commands;
    for (usize i = 0; i < MaximumCommands; ++i) {
        const u32 address = start + static_cast<u32>(i * CommandSize);
        const std::optional<std::span<const u8>> bytes = image.findBytes(address, CommandSize);
        if (!bytes) {
            diagnostics.push_back(base::makeError(
                {}, std::format("the header at 0x{:08X} runs past its segment at 0x{:08X} without "
                                "SCENE_CMD_END",
                                start, address)));
            return commands;
        }
        const Command command{
            .code = (*bytes)[0], .data1 = (*bytes)[1], .data2 = n64::readU32(*bytes, 4)};
        if (isCode(command, CommandCode::End)) {
            return commands;
        }
        commands.push_back(command);
    }
    diagnostics.push_back(base::makeError(
        {}, std::format("the header at 0x{:08X} has no SCENE_CMD_END in its first {} commands",
                        start, MaximumCommands)));
    return commands;
}

std::optional<std::span<const u8>> HeaderReader::findArray(const u32 address, const usize count,
                                                           const usize elementSize,
                                                           const char* what) {
    if (count == 0) {
        return std::span<const u8>{};
    }
    const std::optional<std::span<const u8>> bytes = image.findBytes(address, count * elementSize);
    if (!bytes) {
        diagnostics.push_back(
            base::makeError({}, std::format("the {} at 0x{:08X} ({} entries) is outside the image",
                                            what, address, count)));
    }
    return bytes;
}

std::vector<ActorPlacement> HeaderReader::readActors(const Command& command) {
    std::vector<ActorPlacement> actors;
    const std::optional<std::span<const u8>> bytes =
        findArray(command.data2, command.data1, ActorEntrySize, "actor list");
    if (!bytes) {
        return actors;
    }
    for (usize i = 0; i < command.data1; ++i) {
        const std::span<const u8> entry = bytes->subspan(i * ActorEntrySize, ActorEntrySize);
        actors.push_back(ActorPlacement{.id = n64::readS16(entry, 0),
                                        .position = readVector(entry, 2),
                                        .rotation = readVector(entry, 8),
                                        .params = n64::readS16(entry, 14)});
    }
    return actors;
}

std::vector<TransitionActorPlacement> HeaderReader::readTransitionActors(const Command& command) {
    std::vector<TransitionActorPlacement> actors;
    const std::optional<std::span<const u8>> bytes =
        findArray(command.data2, command.data1, TransitionActorEntrySize, "transition actor list");
    if (!bytes) {
        return actors;
    }
    for (usize i = 0; i < command.data1; ++i) {
        const std::span<const u8> entry =
            bytes->subspan(i * TransitionActorEntrySize, TransitionActorEntrySize);
        actors.push_back(TransitionActorPlacement{
            .rooms = {static_cast<s8>(entry[0]), static_cast<s8>(entry[2])},
            .id = n64::readS16(entry, 4),
            .position = readVector(entry, 6),
            .rotationY = n64::readS16(entry, 12),
            .params = n64::readS16(entry, 14)});
    }
    return actors;
}

std::vector<u32> HeaderReader::readAlternateHeaders(const Command& command) {
    usize count = UnsizedAlternateHeaders;
    if (const std::optional<z64::ImageSymbol> list = image.findSymbolAt(command.data2)) {
        count = list->size / PointerSize;
    }
    std::vector<u32> headers;
    const std::optional<std::span<const u8>> bytes =
        findArray(command.data2, count, PointerSize, "alternate header list");
    if (!bytes) {
        return headers;
    }
    for (usize i = 0; i < count; ++i) {
        headers.push_back(n64::readU32(*bytes, i * PointerSize));
    }
    return headers;
}

std::vector<LightSetting> HeaderReader::readLightSettings(const Command& command) {
    std::vector<LightSetting> settings;
    const std::optional<std::span<const u8>> bytes =
        findArray(command.data2, command.data1, LightSettingSize, "light settings list");
    if (!bytes) {
        return settings;
    }
    for (usize i = 0; i < command.data1; ++i) {
        const std::span<const u8> entry = bytes->subspan(i * LightSettingSize, LightSettingSize);
        settings.push_back(LightSetting{
            .ambientColor = readColor(entry, 0x00),
            .light1Direction = readDirection(entry, 0x03),
            .light1Color = readColor(entry, 0x06),
            .light2Direction = readDirection(entry, 0x09),
            .light2Color = readColor(entry, 0x0C),
            .fogColor = readColor(entry, 0x0F),
            .fogNear = static_cast<s16>(n64::readU16(entry, 0x12) & FogNearMask),
            .zFar = n64::readS16(entry, 0x14),
        });
    }
    return settings;
}

std::vector<RoomDisplayLists> HeaderReader::readEntries(const u32 address, const usize count,
                                                        const usize stride,
                                                        const usize listsOffset) {
    std::vector<RoomDisplayLists> lists;
    const std::optional<std::span<const u8>> bytes =
        findArray(address, count, stride, "room shape's entries");
    if (!bytes) {
        return lists;
    }
    for (usize i = 0; i < count; ++i) {
        const std::span<const u8> entry = bytes->subspan(i * stride, stride);
        lists.push_back(RoomDisplayLists{.opaque = n64::readU32(entry, listsOffset),
                                         .translucent = n64::readU32(entry, listsOffset + 4)});
    }
    return lists;
}

RoomHeader HeaderReader::readRoomShape(const u32 address) {
    RoomHeader room;
    const std::optional<std::span<const u8>> shape = image.findBytes(address, RoomShapeSize);
    if (!shape) {
        diagnostics.push_back(base::makeError(
            {}, std::format("the room shape at 0x{:08X} is outside the image", address)));
        return room;
    }
    const u8 type = (*shape)[0];
    const u8 count = (*shape)[1];
    const u32 entryList = n64::readU32(*shape, 4);
    switch (type) {
    case static_cast<u8>(RoomShapeType::Normal):
        room.shapeType = RoomShapeType::Normal;
        room.displayLists = readEntries(entryList, count, DisplayListsEntrySize, 0);
        break;
    case static_cast<u8>(RoomShapeType::Image):
        room.shapeType = RoomShapeType::Image;
        room.displayLists = readEntries(entryList, 1, DisplayListsEntrySize, 0);
        break;
    case static_cast<u8>(RoomShapeType::Cullable):
        room.shapeType = RoomShapeType::Cullable;
        room.displayLists = readEntries(entryList, count, CullableEntrySize, 8);
        break;
    default:
        diagnostics.push_back(base::makeError(
            {}, std::format("the room shape at 0x{:08X} has type {}, which the game does not draw",
                            address, type)));
        break;
    }
    return room;
}

} // namespace

SceneHeader readSceneHeader(const z64::MemoryImage& image, const u32 address) {
    HeaderReader reader(image);
    SceneHeader scene;
    for (const Command& command : reader.readCommands(address)) {
        if (isCode(command, CommandCode::RoomList)) {
            scene.roomCount = command.data1;
        } else if (isCode(command, CommandCode::PlayerEntryList)) {
            scene.playerEntries = reader.readActors(command);
        } else if (isCode(command, CommandCode::TransitionActorList)) {
            scene.transitionActors = reader.readTransitionActors(command);
        } else if (isCode(command, CommandCode::AlternateHeaderList)) {
            scene.alternateHeaders = reader.readAlternateHeaders(command);
        } else if (isCode(command, CommandCode::LightSettingsList)) {
            scene.lightSettings = reader.readLightSettings(command);
        } else if (isCode(command, CommandCode::SkyboxSettings)) {
            scene.skyboxId = static_cast<u8>(command.data2 >> 24);
            scene.lightMode =
                ((command.data2 >> 8) & 0xFF) == 0 ? LightMode::Time : LightMode::Settings;
        }
    }
    scene.diagnostics = reader.takeDiagnostics();
    return scene;
}

RoomHeader readRoomHeader(const z64::MemoryImage& image, const u32 address) {
    HeaderReader reader(image);
    RoomHeader room;
    bool hasShape = false;
    std::vector<ActorPlacement> actors;
    std::vector<u32> alternateHeaders;
    for (const Command& command : reader.readCommands(address)) {
        if (isCode(command, CommandCode::RoomShape)) {
            room = reader.readRoomShape(command.data2);
            hasShape = true;
        } else if (isCode(command, CommandCode::ActorList)) {
            actors = reader.readActors(command);
        } else if (isCode(command, CommandCode::AlternateHeaderList)) {
            alternateHeaders = reader.readAlternateHeaders(command);
        }
    }
    room.actors = std::move(actors);
    room.alternateHeaders = std::move(alternateHeaders);
    room.diagnostics = reader.takeDiagnostics();
    if (!hasShape) {
        room.diagnostics.push_back(base::makeWarning(
            {}, "the room's header has no SCENE_CMD_ROOM_SHAPE, so there is nothing to draw"));
    }
    return room;
}

} // namespace toki::scene
