#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/HeaderSettings.h"

#include <map>
#include <span>
#include <string>
#include <vector>

namespace cparser {
class MacroTable;
class TypeTable;
} // namespace cparser

namespace toki::scene {

/// A value of a header setting, by the name the checkout gives it, and as
/// it is shown to people: `SKYBOX_NORMAL_SKY` is "Normal Sky".
struct NamedValue {
    std::string name;
    s64 value = 0;
    std::string label;

    friend bool operator==(const NamedValue&, const NamedValue&) = default;
};

/// The names the checkout gives the values of the header settings it names,
/// in the order it defines them.
struct HeaderNames {
    std::map<HeaderSetting, std::vector<NamedValue>> values;
    /// The camera settings a collision's cameras name (`CameraSettingType`).
    std::vector<NamedValue> cameraSettings;

    /// The names of `setting`'s values, empty when the checkout names none.
    [[nodiscard]] std::span<const NamedValue> findValues(HeaderSetting setting) const;
};

/// The names of the settings' values from the enumerations the scene
/// headers declare them with (`SkyboxId`, `NA_BGM`, `RoomType`, ...), and the
/// camera type's from the `SCENE_CAM_TYPE_*` macros, and the camera settings
/// from `CameraSettingType`. An enumeration's count, a name ending in `_MAX`,
/// is left out.
[[nodiscard]] HeaderNames readHeaderNames(const cparser::TypeTable& types,
                                          const cparser::MacroTable& macros);

} // namespace toki::scene
