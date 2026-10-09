#include "EntryEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/FieldsEditor.h"
#include "app/actor/ui/SpawnEditor.h"
#include "app/actor/ui/VariantEditor.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array CollisionKinds{object::AssetKind::Collision};

[[nodiscard]] bool editGeneral(ActorEntry& entry, const EditContext& context) {
    bool isChanged = editText("Name", entry.name);
    isChanged = editText("Description", entry.description) || isChanged;
    isChanged = editNotes("Notes", entry.notes) || isChanged;
    isChanged = editName("Status", entry.status, StatusNames) || isChanged;
    ImGui::SameLine();
    isChanged = ImGui::Checkbox("Uncertain", &entry.isUncertain) || isChanged;
    isChanged = editObject("Object", entry.object, context, "none") || isChanged;
    isChanged = editOptionalNumber("Scale", entry.scale, 0.01F) || isChanged;
    isChanged = editOptionalNumber("Height offset", entry.heightOffset, 0.0F) || isChanged;
    isChanged = editOptionalName("Draw", entry.draw, DrawKindNames, "model") || isChanged;
    ImGui::TextUnformatted("Turned by its rotation about");
    ImGui::SameLine();
    isChanged = ImGui::Checkbox("X", &entry.rotates.x) || isChanged;
    ImGui::SameLine();
    isChanged = ImGui::Checkbox("Y", &entry.rotates.y) || isChanged;
    ImGui::SameLine();
    isChanged = ImGui::Checkbox("Z", &entry.rotates.z) || isChanged;
    return isChanged;
}

[[nodiscard]] bool editShadow(std::optional<Shadow>& shadow) {
    bool hasShadow = shadow.has_value();
    bool isChanged = ImGui::Checkbox("Shadow", &hasShadow);
    if (isChanged) {
        shadow = hasShadow ? std::optional(Shadow{.kind = ShadowKind::Circle, .size = 10.0F})
                           : std::nullopt;
    }
    if (!shadow) {
        return isChanged;
    }
    ImGui::Indent();
    isChanged = editName("Kind", shadow->kind, ShadowKindNames) || isChanged;
    isChanged = ImGui::DragFloat("Size", &shadow->size, 0.1F, 0.0F, 0.0F, "%.4g") || isChanged;
    int alpha = shadow->alpha;
    if (ImGui::SliderInt("Alpha", &alpha, 0, 255)) {
        shadow->alpha = static_cast<u8>(alpha);
        isChanged = true;
    }
    ImGui::Unindent();
    return isChanged;
}

[[nodiscard]] bool editLights(std::vector<PointLight>& lights) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < lights.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        PointLight& light = lights[i];
        std::array<f32, 3> color{static_cast<f32>(light.color[0]) / 255.0F,
                                 static_cast<f32>(light.color[1]) / 255.0F,
                                 static_cast<f32>(light.color[2]) / 255.0F};
        if (ImGui::ColorEdit3("Colour", color.data(), ImGuiColorEditFlags_Uint8)) {
            for (usize channel = 0; channel < 3; ++channel) {
                light.color[channel] = static_cast<u8>(color[channel] * 255.0F + 0.5F);
            }
            isChanged = true;
        }
        isChanged =
            ImGui::DragFloat("Radius", &light.radius, 1.0F, 0.0F, 0.0F, "%.4g") || isChanged;
        isChanged = editVector("Offset", light.offset) || isChanged;
        if (ImGui::SmallButton("Remove this light")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("Add a light")) {
        lights.push_back(PointLight{.color = {255, 255, 255}, .radius = 100.0F});
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool editTextIds(std::vector<u16>& ids) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < ids.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        isChanged = editHex("##id", ids[i]) || isChanged;
        ImGui::SameLine();
        if (drawIconButton(core::icons::Trash, "Remove this text ID")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("Add a text ID")) {
        ids.push_back(0);
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool editExtras(ActorEntry& entry, const EditContext& context) {
    bool isChanged = editShadow(entry.shadow);
    isChanged = drawSpawnEditor(entry.spawn, entry.fields) || isChanged;
    isChanged =
        editOptionalSymbol("Collision", entry.collision, entry.object, CollisionKinds, context) ||
        isChanged;
    if (ImGui::TreeNode("Lights")) {
        isChanged = editLights(entry.lights) || isChanged;
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Text IDs")) {
        isChanged = editTextIds(entry.textIds) || isChanged;
        ImGui::TreePop();
    }
    return isChanged;
}

} // namespace

bool drawEntryEditor(ActorEntry& entry, const EditContext& context) {
    bool isChanged = false;
    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
        isChanged = editGeneral(entry, context) || isChanged;
    }
    if (ImGui::CollapsingHeader("Fields", ImGuiTreeNodeFlags_DefaultOpen)) {
        isChanged = drawFieldsEditor(entry.fields) || isChanged;
    }
    ImGui::SeparatorText("Variants, tried in order");
    isChanged = drawVariantsEditor(entry, context) || isChanged;
    if (ImGui::CollapsingHeader("Shadow, spawn, collision, lights and text")) {
        isChanged = editExtras(entry, context) || isChanged;
    }
    return isChanged;
}

} // namespace toki::actor
