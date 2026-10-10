#include "EntryEditor.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"

#include <imgui.h>

#include <optional>

namespace toki::actor {

namespace {

constexpr f32 DefaultScale = 0.01F;

[[nodiscard]] bool editDraw(ActorEntry& entry) {
    DrawKind draw = entry.draw.value_or(DrawKind::Model);
    if (!editName("Draw as", draw, DrawKindNames)) {
        return false;
    }
    entry.draw = draw == DrawKind::Model ? std::nullopt : std::optional(draw);
    return true;
}

[[nodiscard]] bool editRotates(RotationAxes& rotates) {
    drawPropertyLabel("Turned about");
    bool isChanged = ImGui::Checkbox("X", &rotates.x);
    ImGui::SameLine();
    isChanged = ImGui::Checkbox("Y", &rotates.y) || isChanged;
    ImGui::SameLine();
    isChanged = ImGui::Checkbox("Z", &rotates.z) || isChanged;
    ImGui::SetItemTooltip("Which axes of its placed rotation the actor is drawn turned by");
    return isChanged;
}

} // namespace

bool drawGeneralEditor(ActorEntry& entry, const EditContext& context) {
    if (!beginProperties("general")) {
        return false;
    }
    bool isChanged = editText("Name", entry.name);
    isChanged = editNotes("Notes", entry.notes) || isChanged;
    isChanged = editObject("Object", entry.object, context, "none") || isChanged;
    isChanged = editOptionalNumber("Scale", entry.scale, DefaultScale) || isChanged;
    isChanged = editOptionalNumber("Height offset", entry.heightOffset, 0.0F) || isChanged;
    isChanged = editDraw(entry) || isChanged;
    isChanged = editRotates(entry.rotates) || isChanged;
    endProperties();
    return isChanged;
}

} // namespace toki::actor
