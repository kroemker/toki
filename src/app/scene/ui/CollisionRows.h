#pragma once

#include "app/core/ui/NumberField.h"
#include "app/scene/Types.h"
#include "app/scene/domain/HeaderNames.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace toki::scene {

struct Collision;
struct Scene;

/// The rows the collision forms share: a title, which layers read the
/// collision, the camera a surface or a waterbox names and its setting, and
/// numbers and named values, each returning the value picked once one is.
class CollisionRows {
public:
    /// The form's icon, name and what it is part of. `selected` is how many
    /// nodes are selected, of which this is the last.
    static void drawTitle(const char* icon, const std::string& name, const std::string& detail,
                          usize selected);

    /// Says which layers read the layer's collision, and true when "Make Own
    /// Copy for This Layer" is clicked, which only several layers offer.
    [[nodiscard]] static bool drawSharing(const Scene& scene, usize layer);

    /// The index into the collision's camera list, each camera by its setting.
    [[nodiscard]] std::optional<u8> drawCameraChoice(const Scene& scene, const Collision& collision,
                                                     u8 index);

    /// A camera setting, by the names the checkout gives them.
    [[nodiscard]] std::optional<u16> drawCameraSettingChoice(const Scene& scene, u16 setting);

    /// A dropdown of `values`, filtered once there are many, the value shown
    /// as a number where none names it.
    [[nodiscard]] std::optional<s64> drawNamedChoice(std::string_view id,
                                                     std::span<const NamedValue> values, s64 value);

    [[nodiscard]] std::optional<s64> drawNumber(std::string_view id, s64 value, s64 minimum,
                                                s64 maximum) {
        return numberField.draw(id, value, minimum, maximum);
    }

    /// A checkbox alone in its row, showing a mix of values as such.
    [[nodiscard]] static std::optional<bool> drawToggle(std::string_view id, bool value,
                                                        bool isMixed = false);

private:
    core::NumberField numberField;
    std::string filter;
};

} // namespace toki::scene
