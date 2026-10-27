#include "ComponentForm.h"

#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/ComponentSummary.h"
#include "app/scene/domain/SceneMaterials.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

constexpr f32 ColorScale = 255.0F;
constexpr s32 AlphaMax = 255;

void drawMuted(const std::string& text) {
    ImGui::TextColored(core::palette::TextMuted, "%s", text.c_str());
}

[[nodiscard]] std::string joinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const std::string& name : names) {
        joined += joined.empty() ? name : ", " + name;
    }
    return joined.empty() ? std::string("None") : joined;
}

[[nodiscard]] u8 makeByte(const f32 value) {
    return static_cast<u8>(std::lround(std::clamp(value, 0.0F, 1.0F) * ColorScale));
}

} // namespace

ComponentEdit ComponentForm::draw(const ComponentSummary& summary,
                                  const std::vector<SceneMaterial>& materials,
                                  const bool isLinked) {
    ComponentEdit edit;
    drawMuted(std::format("{} vertices, {} faces picked", summary.vertices, summary.faces));
    if (summary.vertices > 0) {
        drawVertices(summary, edit);
    }
    if (summary.faces > 0) {
        drawFaces(summary, materials, edit);
    }
    if (ImGui::CollapsingHeader("Collision Below", ImGuiTreeNodeFlags_DefaultOpen) &&
        core::beginProperties("##collisionBelow")) {
        core::drawPropertyLabel("Follows Mesh",
                                "Whether collision vertices where the picked vertices stand move "
                                "with them. The viewport bar's link toggle sets the same.");
        bool isOn = isLinked;
        if (ImGui::Checkbox("##follows", &isOn)) {
            edit.isLinked = isOn;
        }
        core::endProperties();
    }
    drawSource(summary);
    return edit;
}

void ComponentForm::drawVertices(const ComponentSummary& summary, ComponentEdit& edit) {
    if (!ImGui::CollapsingHeader("Vertices", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##vertices")) {
        return;
    }
    core::drawPropertyLabel("Position", "The middle of the picked vertices, in the world. "
                                        "Changing it moves them all by as much.");
    if (!isMiddleHeld) {
        middle = summary.middle;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat3("##position", &middle.x, 1.0F, 0.0F, 0.0F, "%.0f");
    isMiddleHeld = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        edit.middle = middle;
    }
    if (summary.hasUnlit) {
        core::drawPropertyLabel("Color", summary.color
                                             ? "The colour of the picked vertices' entries that "
                                               "are not lit."
                                             : "The picked vertices' entries are coloured "
                                               "differently. A colour picked gives them all it.");
        if (!isColorHeld && summary.color) {
            for (usize channel = 0; channel < 3; ++channel) {
                color[channel] = static_cast<f32>((*summary.color)[channel]) / ColorScale;
            }
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::ColorEdit3(summary.color ? "##color" : "(Mixed)##color", color.data(),
                          ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_NoPicker);
        isColorHeld = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edit.color = std::array{makeByte(color[0]), makeByte(color[1]), makeByte(color[2])};
        }
    } else if (summary.hasLit) {
        core::drawPropertyLabel("Color", "Lit vertices hold a normal where an unlit one holds a "
                                         "colour, made again whenever they move.");
        drawMuted("Lit");
    }
    core::drawPropertyLabel("Alpha", summary.alpha ? "The alpha of every entry of the picked "
                                                     "vertices."
                                                   : "The picked vertices' alpha differs. A "
                                                     "value given sets it on them all.");
    if (!isAlphaHeld) {
        alpha = summary.alpha ? s32{*summary.alpha} : AlphaMax;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##alpha", &alpha, 0, AlphaMax, summary.alpha ? "%d" : "(Mixed)");
    isAlphaHeld = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        edit.alpha = static_cast<u8>(alpha);
    }
    core::endProperties();
}

void ComponentForm::drawFaces(const ComponentSummary& summary,
                              const std::vector<SceneMaterial>& materials, ComponentEdit& edit) {
    if (!ImGui::CollapsingHeader("Faces", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##faces")) {
        return;
    }
    core::drawPropertyLabel("Material", "What the picked faces are drawn under. Picking one "
                                        "moves them into the run that draws it in their room.");
    const std::string shown = summary.material && *summary.material < materials.size()
                                  ? materials[*summary.material].name
                                  : std::string("(Mixed)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##material", shown.c_str())) {
        for (usize material = 0; material < materials.size(); ++material) {
            ImGui::PushID(static_cast<int>(material));
            if (ImGui::Selectable(materials[material].name.c_str(), summary.material == material)) {
                edit.material = material;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    core::drawPropertyLabel("Pass", "Whether the faces are drawn with the room's opaque list or "
                                    "its translucent one, as the run that draws them is.");
    drawMuted(!summary.isTranslucent   ? "(Mixed)"
              : *summary.isTranslucent ? "Translucent"
                                       : "Opaque");
    core::endProperties();
}

void ComponentForm::drawSource(const ComponentSummary& summary) {
    if (!ImGui::CollapsingHeader("Source") || !core::beginProperties("##source")) {
        return;
    }
    core::drawPropertyLabel("Vertices In", "The Vtx arrays the picked vertices are loaded from.");
    ImGui::TextWrapped("%s", joinNames(summary.vertexArrays).c_str());
    core::drawPropertyLabel("Drawn By", "The display lists whose triangles draw the picked faces.");
    ImGui::TextWrapped("%s", joinNames(summary.lists).c_str());
    core::drawPropertyLabel("Rewritten On Save",
                            "What a save writes again of them: a moved vertex only its Vtx, a "
                            "face added, taken out or moved the run of the list that draws it.");
    ImGui::TextWrapped("%s", summary.lists.empty() ? joinNames(summary.vertexArrays).c_str()
                                                   : "Their Vtx, and the runs a topology edit "
                                                     "rebuilds");
    core::endProperties();
}

} // namespace toki::scene
