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

/// A value of a header setting, by the name the checkout gives it.
struct NamedValue {
    std::string name;
    s64 value = 0;

    friend bool operator==(const NamedValue&, const NamedValue&) = default;
};

/// The names the checkout gives the values of the header settings it names,
/// in the order it defines them.
struct HeaderNames {
    std::map<HeaderSetting, std::vector<NamedValue>> values;

    /// The names of `setting`'s values, empty when the checkout names none.
    [[nodiscard]] std::span<const NamedValue> findValues(HeaderSetting setting) const;
};

/// The names of the settings' values from the enumerations the scene
/// headers declare them with (`SkyboxId`, `NA_BGM`, `RoomType`, ...), and the
/// camera type's from the `SCENE_CAM_TYPE_*` macros. An enumeration's count,
/// a name ending in `_MAX`, is left out.
[[nodiscard]] HeaderNames readHeaderNames(const cparser::TypeTable& types,
                                          const cparser::MacroTable& macros);

} // namespace toki::scene
