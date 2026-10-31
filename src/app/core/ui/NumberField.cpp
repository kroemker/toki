#include "NumberField.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>

namespace toki::core {

std::optional<s64> NumberField::draw(const std::string_view id, const s64 value, const s64 minimum,
                                     const s64 maximum) {
    const std::string key(id);
    const bool isHeld = held && held->id == key;
    s64 shown = isHeld ? held->value : value;
    ImGui::InputScalar(key.c_str(), ImGuiDataType_S64, &shown);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        held.reset();
        const s64 clamped = std::clamp(shown, minimum, maximum);
        if (clamped != value) {
            return clamped;
        }
    } else if (ImGui::IsItemActive()) {
        held = HeldValue{.id = key, .value = shown};
    } else if (isHeld) {
        held.reset();
    }
    return std::nullopt;
}

} // namespace toki::core
