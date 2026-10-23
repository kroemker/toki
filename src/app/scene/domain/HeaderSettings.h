#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"

#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace toki::scene {

class SceneModel;

/// A setting a header command holds in its own bytes, rather than in a list
/// it points at. The first ten are a scene's, the rest a room's.
enum class HeaderSetting : u8 {
    SoundSpec,
    NatureAmbience,
    Sequence,
    Skybox,
    SkyboxConfig,
    LightMode,
    NaviHints,
    KeepObject,
    CameraType,
    MapLocation,
    RoomType,
    Environment,
    LensMode,
    DisablesWarpSongs,
    Hour,
    Minute,
    TimeSpeed,
    DisablesSky,
    DisablesSunMoon,
    WindX,
    WindY,
    WindZ,
    WindStrength,
    Echo,
};

/// The lowest and highest value a setting's bits hold.
struct SettingRange {
    s64 minimum = 0;
    s64 maximum = 0;
};

/// The command that holds `setting`.
[[nodiscard]] CommandCode getSettingCommand(HeaderSetting setting);

/// Whether a room's header holds `setting`, rather than the scene's.
[[nodiscard]] bool isRoomSetting(HeaderSetting setting);

[[nodiscard]] SettingRange getSettingRange(HeaderSetting setting);

/// The value of `setting` in a header's `commands`, or nothing when the
/// header has no command that holds it.
[[nodiscard]] std::optional<s64> findSetting(std::span<const HeaderCommand> commands,
                                             HeaderSetting setting);

/// Which header an edit of a setting changes: the scene's, or `room`'s, for
/// `layer`, the game's scene layer. A room with no header of its own for the
/// layer is edited in its main header, which is what the game reads for it.
struct HeaderRef {
    usize layer = 0;
    std::optional<usize> room;
};

/// Sets `setting` to `value` in the header `where` names, made its own for
/// the layer first when another layer reads it too. A header without the
/// command that holds it is given one, its other settings as
/// `addSettingCommand` makes them.
[[nodiscard]] std::expected<void, base::Diagnostic>
setHeaderSetting(SceneModel& model, const HeaderRef& where, HeaderSetting setting, s64 value);

/// Adds the command that holds `setting` before the header's end, with the
/// values that change nothing where the game has such a value (a time of
/// 0xFF, which leaves the time as it was), zero otherwise. A header that has
/// it already is left as it is.
[[nodiscard]] std::expected<void, base::Diagnostic>
addSettingCommand(SceneModel& model, const HeaderRef& where, HeaderSetting setting);

/// The `SCENE_CMD_*` macro of `code`, for what a header lacks.
[[nodiscard]] std::string_view getCommandMacro(CommandCode code);

} // namespace toki::scene
