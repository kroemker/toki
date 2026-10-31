#include "MaterialForm.h"

#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/SceneMaterials.h"

#include <imgui.h>

#include <cfloat>
#include <format>
#include <optional>
#include <string>

namespace toki::scene {

namespace {

void drawMuted(const std::string& text) {
    ImGui::TextColored(core::palette::TextMuted, "%s", text.c_str());
}

[[nodiscard]] std::optional<std::optional<MaterialBinding>> drawBinding(const BindingRows& rows) {
    std::optional<std::optional<MaterialBinding>> picked;
    std::string warning = rows.problem;
    if (warning.empty() && rows.unbound > 0) {
        warning = std::format("{} of its faces are not as it is bound. Re-apply makes them so.",
                              rows.unbound);
    }
    core::drawPropertyLabel("Collision",
                            "What the faces drawn under the material are made of. Bound, their "
                            "collision follows the material and is set here alone; unbound, each "
                            "face is set on its own.",
                            warning);
    const bool canReapply = rows.unbound > 0 && rows.current < rows.choices.size() &&
                            rows.choices[rows.current].binding;
    if (canReapply) {
        ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("Re-apply").x +
                                  (ImGui::GetStyle().FramePadding.x * 2.0F) +
                                  ImGui::GetStyle().ItemSpacing.x));
    } else {
        ImGui::SetNextItemWidth(-FLT_MIN);
    }
    const char* shown =
        rows.current < rows.choices.size() ? rows.choices[rows.current].label.c_str() : "";
    if (ImGui::BeginCombo("##binding", shown, ImGuiComboFlags_HeightLarge)) {
        for (usize choice = 0; choice < rows.choices.size(); ++choice) {
            const BindingChoice& offered = rows.choices[choice];
            const std::string label = offered.changed > 0 && choice != rows.current
                                          ? std::format("{}  ({} faces change)###{}", offered.label,
                                                        offered.changed, choice)
                                          : std::format("{}###{}", offered.label, choice);
            if (ImGui::Selectable(label.c_str(), choice == rows.current) &&
                choice != rows.current) {
                picked = offered.binding;
            }
        }
        ImGui::EndCombo();
    }
    if (canReapply) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Re-apply")) {
            picked = rows.choices[rows.current].binding;
        }
    }
    return picked;
}

} // namespace

std::optional<std::optional<MaterialBinding>>
MaterialForm::draw(const SceneMaterial& material, const std::optional<BindingRows>& binding) {
    if (!ImGui::CollapsingHeader("Render Material Settings", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##renderMaterial")) {
        return std::nullopt;
    }
    core::drawPropertyLabel("Name", "What the material is called. Right-click it under Materials "
                                    "in the Scene panel to rename it.");
    ImGui::TextUnformatted(material.name.c_str());
    core::drawPropertyLabel("Texture", "The texture the material draws with.");
    drawMuted(material.texture.empty() ? std::string("None") : material.texture);
    core::drawPropertyLabel("Pass", "Whether its faces are drawn with their rooms' opaque lists "
                                    "or their translucent ones.");
    drawMuted(material.isTranslucent ? "Translucent" : "Opaque");
    core::drawPropertyLabel("Shading", "Whether its faces' vertices hold normals the game lights, "
                                       "or colours of their own.");
    drawMuted(material.isLit ? "Lit" : "Vertex Colored");
    core::drawPropertyLabel("Faces", "How many faces are drawn under it, and in how many rooms.");
    drawMuted(material.rooms.size() == 1
                  ? std::format("{}", material.countFaces())
                  : std::format("{} in {} rooms", material.countFaces(), material.rooms.size()));
    std::optional<std::optional<MaterialBinding>> picked;
    if (binding) {
        picked = drawBinding(*binding);
    }
    core::endProperties();
    return picked;
}

void MaterialForm::drawMixed() {
    if (!ImGui::CollapsingHeader("Render Material Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::PushTextWrapPos(0.0F);
    drawMuted("The picked faces are drawn under several materials. Select one under Materials in "
              "the Scene panel to see its settings.");
    ImGui::PopTextWrapPos();
}

} // namespace toki::scene
