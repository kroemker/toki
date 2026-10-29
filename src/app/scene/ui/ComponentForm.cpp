#include "ComponentForm.h"

#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/ComponentSummary.h"
#include "app/scene/domain/SceneMaterials.h"
#include "app/scene/ui/CollisionRows.h"

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

[[nodiscard]] u8 makeByte(const f32 value) {
    return static_cast<u8>(std::lround(std::clamp(value, 0.0F, 1.0F) * ColorScale));
}

} // namespace

ComponentEdit ComponentForm::draw(const ComponentSummary& summary,
                                  const std::vector<SceneMaterial>& materials,
                                  const std::optional<SurfaceRows>& surface) {
    ComponentEdit edit;
    const usize solidOnly = surface ? surface->solidOnly : 0;
    drawMuted(solidOnly > 0 ? std::format("{} vertices, {} faces and {} polygons no room draws "
                                          "picked",
                                          summary.vertices, summary.faces, solidOnly)
                            : std::format("{} vertices, {} faces picked", summary.vertices,
                                          summary.faces));
    if (summary.vertices > 0) {
        drawVertices(summary, edit);
    }
    if (summary.faces > 0) {
        drawFaces(summary, materials, edit);
    }
    if (surface && surface->drawn + surface->solidOnly > 0) {
        drawSurface(*surface, edit);
    }
    return edit;
}

void ComponentForm::drawSurface(const SurfaceRows& surface, ComponentEdit& edit) {
    if (!ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##surface")) {
        return;
    }
    const usize total = surface.drawn + surface.solidOnly;
    const bool canUndraw =
        surface.solidOnly == 0 && surface.drawn > 0 && surface.solid == surface.drawn;
    core::drawPropertyLabel(
        "Drawn",
        "Whether a room draws the picked faces. A solid face no room draws is collision alone, "
        "as an invisible wall is: turning it off takes the faces out of their rooms' lists and "
        "keeps their polygons.",
        canUndraw || surface.drawn == 0 ? ""
                                        : "Only solid faces can stop being drawn; the others "
                                          "would be gone. A face no room draws is not drawn "
                                          "again from here.");
    ImGui::BeginDisabled(!canUndraw);
    if (const std::optional<bool> toggled =
            CollisionRows::drawToggle("##drawn", surface.solidOnly == 0,
                                      surface.drawn > 0 && surface.solidOnly > 0);
        toggled && !*toggled) {
        edit.isDrawn = false;
    }
    ImGui::EndDisabled();
    const usize solid = surface.solid + surface.solidOnly;
    core::drawPropertyLabel("Solid",
                            "Whether the picked faces have a polygon of the collision, so that "
                            "Link stands on them and walks into them. A face made solid is made "
                            "of what its material's other faces are made of.");
    if (const std::optional<bool> toggled =
            CollisionRows::drawToggle("##solid", solid == total, solid > 0 && solid < total)) {
        edit.isSolid = *toggled;
    }
    core::drawPropertyLabel("Collision Material",
                            "What the polygons of the picked faces are made of: a surface type "
                            "and what passes through them. Picking one makes every picked face "
                            "solid with it.");
    std::string shown = "None";
    if (surface.material && *surface.material < surface.names.size()) {
        shown = surface.names[*surface.material];
    } else if (solid > 0) {
        shown = "(Mixed)";
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##collisionMaterial", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (usize material = 0; material < surface.names.size(); ++material) {
            ImGui::PushID(static_cast<int>(material));
            if (ImGui::Selectable(surface.names[material].c_str(), surface.material == material)) {
                edit.collisionMaterial = material;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    core::endProperties();
}

void ComponentForm::drawVertices(const ComponentSummary& summary, ComponentEdit& edit) {
    if (!ImGui::CollapsingHeader("Vertices", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##vertices")) {
        return;
    }
    core::drawPropertyLabel("Position",
                            "The middle of the picked vertices, in the world. Changing it moves "
                            "them all by as much.",
                            summary.isShared ? "Some of them are in the scene's own segment: "
                                               "moving them moves them in every room that "
                                               "draws them."
                                             : "");
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

} // namespace toki::scene
