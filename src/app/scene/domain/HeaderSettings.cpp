#include "HeaderSettings.h"

#include "app/scene/Types.h"
#include "app/scene/domain/HeaderSymbols.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "z64/SymbolData.h"

#include <algorithm>
#include <array>
#include <bit>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace toki::scene {

namespace {

/// Where a setting's bits are in its command: a big-endian field of `size`
/// bytes at `offset`, masked.
struct SettingSpec {
    CommandCode code = CommandCode::End;
    usize offset = 0;
    usize size = 1;
    u32 mask = 0xFF;
    bool isSigned = false;
};

[[nodiscard]] SettingSpec getSpec(const HeaderSetting setting) {
    using enum HeaderSetting;
    constexpr usize Data1 = 1;
    constexpr usize Word = 4;
    switch (setting) {
    case SoundSpec:
        return {.code = CommandCode::SoundSettings, .offset = Data1};
    case NatureAmbience:
        return {.code = CommandCode::SoundSettings, .offset = Word + 2};
    case Sequence:
        return {.code = CommandCode::SoundSettings, .offset = Word + 3};
    case Skybox:
        return {.code = CommandCode::SkyboxSettings, .offset = Word};
    case SkyboxConfig:
        return {.code = CommandCode::SkyboxSettings, .offset = Word + 1};
    case LightMode:
        return {.code = CommandCode::SkyboxSettings, .offset = Word + 2};
    case NaviHints:
        return {.code = CommandCode::SpecialFiles, .offset = Data1};
    case KeepObject:
        return {.code = CommandCode::SpecialFiles, .offset = Word, .size = 4, .mask = 0xFFFF};
    case CameraType:
        return {.code = CommandCode::MiscSettings, .offset = Data1};
    case MapLocation:
        return {.code = CommandCode::MiscSettings, .offset = Word, .size = 4, .mask = 0xFFFF'FFFF};
    case RoomType:
        return {.code = CommandCode::RoomBehavior, .offset = Data1};
    case Environment:
        return {.code = CommandCode::RoomBehavior, .offset = Word, .size = 4, .mask = 0xFF};
    case LensMode:
        return {.code = CommandCode::RoomBehavior, .offset = Word, .size = 4, .mask = 0x100};
    case DisablesWarpSongs:
        return {.code = CommandCode::RoomBehavior, .offset = Word, .size = 4, .mask = 0x400};
    case Hour:
        return {.code = CommandCode::TimeSettings, .offset = Word};
    case Minute:
        return {.code = CommandCode::TimeSettings, .offset = Word + 1};
    case TimeSpeed:
        return {.code = CommandCode::TimeSettings, .offset = Word + 2, .isSigned = true};
    case DisablesSky:
        return {.code = CommandCode::SkyboxDisables, .offset = Word};
    case DisablesSunMoon:
        return {.code = CommandCode::SkyboxDisables, .offset = Word + 1};
    case WindX:
        return {.code = CommandCode::WindSettings, .offset = Word, .isSigned = true};
    case WindY:
        return {.code = CommandCode::WindSettings, .offset = Word + 1, .isSigned = true};
    case WindZ:
        return {.code = CommandCode::WindSettings, .offset = Word + 2, .isSigned = true};
    case WindStrength:
        return {.code = CommandCode::WindSettings, .offset = Word + 3};
    case Echo:
        return {.code = CommandCode::EchoSettings, .offset = Word + 3};
    }
    return {};
}

[[nodiscard]] u32 readField(const std::span<const u8> command, const SettingSpec& spec) {
    u32 raw = 0;
    for (usize i = 0; i < spec.size; ++i) {
        raw = (raw << 8) | command[spec.offset + i];
    }
    return raw;
}

void writeField(const std::span<u8> command, const SettingSpec& spec, u32 raw) {
    for (usize i = spec.size; i > 0; --i) {
        command[spec.offset + i - 1] = static_cast<u8>(raw);
        raw >>= 8;
    }
}

[[nodiscard]] s64 readSetting(const std::span<const u8> command, const SettingSpec& spec) {
    const u32 bits = (readField(command, spec) & spec.mask) >> std::countr_zero(spec.mask);
    const int width = std::popcount(spec.mask);
    if (spec.isSigned && ((bits >> (width - 1)) & 1) != 0) {
        return static_cast<s64>(bits) - (s64{1} << width);
    }
    return bits;
}

void writeSetting(const std::span<u8> command, const SettingSpec& spec, const s64 value) {
    const u32 bits = (static_cast<u32>(value) << std::countr_zero(spec.mask)) & spec.mask;
    writeField(command, spec, (readField(command, spec) & ~spec.mask) | bits);
}

/// The bytes a command added for a setting starts with, past its ID.
void writeDefaults(const std::span<u8> command, const CommandCode code) {
    constexpr u8 UnsetTime = 0xFF;
    if (code == CommandCode::TimeSettings) {
        writeSetting(command, getSpec(HeaderSetting::Hour), UnsetTime);
        writeSetting(command, getSpec(HeaderSetting::Minute), UnsetTime);
    }
}

[[nodiscard]] std::expected<std::string, base::Diagnostic>
ownSettingHeader(SceneModel& model, const HeaderRef& where, const HeaderSetting setting) {
    if (isRoomSetting(setting) != where.room.has_value()) {
        return std::unexpected(base::makeError(
            {}, std::format("{} is in a {}'s header", getCommandMacro(getSettingCommand(setting)),
                            isRoomSetting(setting) ? "room" : "scene")));
    }
    return ownHeader(model, {.image = where.room ? *where.room + 1 : 0, .layer = where.layer});
}

/// The offset of the command that holds `setting` in `header`, added when it
/// has none.
[[nodiscard]] std::expected<usize, base::Diagnostic> findOrAddCommand(z64::SymbolData& header,
                                                                      const HeaderSetting setting) {
    const CommandCode code = getSettingCommand(setting);
    if (const std::optional<usize> found = findCommand(header, code)) {
        return *found;
    }
    const std::optional<usize> added = insertCommand(header, code);
    if (!added) {
        return std::unexpected(
            base::makeError({}, std::format("'{}' has no SCENE_CMD_END to add {} before",
                                            header.name, getCommandMacro(code))));
    }
    writeDefaults(std::span(header.bytes).subspan(*added, HeaderCommandSize), code);
    return *added;
}

} // namespace

CommandCode getSettingCommand(const HeaderSetting setting) {
    return getSpec(setting).code;
}

bool isRoomSetting(const HeaderSetting setting) {
    return setting >= HeaderSetting::RoomType;
}

SettingRange getSettingRange(const HeaderSetting setting) {
    const SettingSpec spec = getSpec(setting);
    const int width = std::popcount(spec.mask);
    if (spec.isSigned) {
        return {.minimum = -(s64{1} << (width - 1)), .maximum = (s64{1} << (width - 1)) - 1};
    }
    return {.minimum = 0, .maximum = (s64{1} << width) - 1};
}

std::optional<s64> findSetting(const std::span<const HeaderCommand> commands,
                               const HeaderSetting setting) {
    const SettingSpec spec = getSpec(setting);
    const auto found =
        std::ranges::find(commands, static_cast<u8>(spec.code), &HeaderCommand::code);
    if (found == commands.end()) {
        return std::nullopt;
    }
    const std::array<u8, HeaderCommandSize> bytes{found->code,
                                                  found->data1,
                                                  0,
                                                  0,
                                                  static_cast<u8>(found->data2 >> 24),
                                                  static_cast<u8>(found->data2 >> 16),
                                                  static_cast<u8>(found->data2 >> 8),
                                                  static_cast<u8>(found->data2)};
    return readSetting(bytes, spec);
}

std::expected<void, base::Diagnostic> setHeaderSetting(SceneModel& model, const HeaderRef& where,
                                                       const HeaderSetting setting,
                                                       const s64 value) {
    const SettingRange range = getSettingRange(setting);
    if (value < range.minimum || value > range.maximum) {
        return std::unexpected(
            base::makeError({}, std::format("{} holds values from {} to {}, not {}",
                                            getCommandMacro(getSettingCommand(setting)),
                                            range.minimum, range.maximum, value)));
    }
    const std::expected<std::string, base::Diagnostic> name =
        ownSettingHeader(model, where, setting);
    if (!name) {
        return std::unexpected(name.error());
    }
    z64::SymbolData header = *model.findSymbol(*name);
    const std::expected<usize, base::Diagnostic> command = findOrAddCommand(header, setting);
    if (!command) {
        return std::unexpected(command.error());
    }
    writeSetting(std::span(header.bytes).subspan(*command, HeaderCommandSize), getSpec(setting),
                 value);
    return model.setSymbol(std::move(header));
}

std::expected<void, base::Diagnostic> addSettingCommand(SceneModel& model, const HeaderRef& where,
                                                        const HeaderSetting setting) {
    const std::expected<std::string, base::Diagnostic> name =
        ownSettingHeader(model, where, setting);
    if (!name) {
        return std::unexpected(name.error());
    }
    z64::SymbolData header = *model.findSymbol(*name);
    if (findCommand(header, getSettingCommand(setting))) {
        return {};
    }
    if (const std::expected<usize, base::Diagnostic> command = findOrAddCommand(header, setting);
        !command) {
        return std::unexpected(command.error());
    }
    return model.setSymbol(std::move(header));
}

std::string_view getCommandMacro(const CommandCode code) {
    switch (code) {
    case CommandCode::WindSettings:
        return "SCENE_CMD_WIND_SETTINGS";
    case CommandCode::SpecialFiles:
        return "SCENE_CMD_SPECIAL_FILES";
    case CommandCode::RoomBehavior:
        return "SCENE_CMD_ROOM_BEHAVIOR";
    case CommandCode::TimeSettings:
        return "SCENE_CMD_TIME_SETTINGS";
    case CommandCode::SkyboxSettings:
        return "SCENE_CMD_SKYBOX_SETTINGS";
    case CommandCode::SkyboxDisables:
        return "SCENE_CMD_SKYBOX_DISABLES";
    case CommandCode::SoundSettings:
        return "SCENE_CMD_SOUND_SETTINGS";
    case CommandCode::EchoSettings:
        return "SCENE_CMD_ECHO_SETTINGS";
    case CommandCode::MiscSettings:
        return "SCENE_CMD_MISC_SETTINGS";
    case CommandCode::PlayerEntryList:
        return "SCENE_CMD_PLAYER_ENTRY_LIST";
    case CommandCode::ActorList:
        return "SCENE_CMD_ACTOR_LIST";
    case CommandCode::RoomList:
        return "SCENE_CMD_ROOM_LIST";
    case CommandCode::RoomShape:
        return "SCENE_CMD_ROOM_SHAPE";
    case CommandCode::ObjectList:
        return "SCENE_CMD_OBJECT_LIST";
    case CommandCode::TransitionActorList:
        return "SCENE_CMD_TRANSITION_ACTOR_LIST";
    case CommandCode::LightSettingsList:
        return "SCENE_CMD_ENV_LIGHT_SETTINGS";
    case CommandCode::End:
        return "SCENE_CMD_END";
    case CommandCode::AlternateHeaderList:
        return "SCENE_CMD_ALTERNATE_HEADER_LIST";
    }
    return "SCENE_CMD";
}

} // namespace toki::scene
