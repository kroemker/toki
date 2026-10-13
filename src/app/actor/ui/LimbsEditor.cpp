#include "LimbsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/core/ui/PropertyTable.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array DisplayListKinds{object::AssetKind::DisplayList};
constexpr int LastLimb = 0xFE;
constexpr std::array<const char*, 2> DrawLabels{"Nothing", "A display list"};

[[nodiscard]] const std::vector<CatalogLimb>* findLimbs(const std::string& skeleton,
                                                        const std::optional<std::string>& object,
                                                        const EditContext& context) {
    return object && !skeleton.empty() ? context.symbolCatalog.findLimbs(*object, skeleton)
                                       : nullptr;
}

[[nodiscard]] std::string makeLimbLabel(const u8 limb, const std::vector<CatalogLimb>* limbs) {
    const usize index = limb - 1U;
    if (limbs == nullptr || index >= limbs->size() || (*limbs)[index].name.empty()) {
        return std::format("Limb {}", limb);
    }
    return std::format("{}  {}", limb, (*limbs)[index].name);
}

[[nodiscard]] bool editDrawn(LimbOverride& limb, const std::optional<std::string>& object,
                             const std::vector<CatalogLimb>* limbs, const EditContext& context) {
    core::drawPropertyLabel(makeLimbLabel(limb.limb, limbs).c_str());
    bool isChanged = false;
    const bool isList = limb.displayList.has_value();
    if (ImGui::BeginCombo("##drawn", DrawLabels[isList ? 1 : 0])) {
        if (ImGui::Selectable(DrawLabels[0], !isList) && isList) {
            limb.displayList.reset();
            limb.object.reset();
            isChanged = true;
        }
        if (ImGui::Selectable(DrawLabels[1], isList) && !isList) {
            limb.displayList = std::string();
            isChanged = true;
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("What the limb draws in place of its own display list, as "
                          "OverrideLimbDraw swaps it");
    if (!limb.displayList) {
        return isChanged;
    }
    isChanged = editSymbol("Display list", *limb.displayList, limb.object ? limb.object : object,
                           DisplayListKinds, context) ||
                isChanged;
    isChanged =
        editObject("From object", limb.object, context, object.value_or("none")) || isChanged;
    return isChanged;
}

[[nodiscard]] std::optional<u8> pickLimb(const char* label, const std::vector<CatalogLimb>* limbs,
                                         const std::vector<LimbOverride>& taken) {
    std::optional<u8> picked;
    if (limbs == nullptr) {
        int typed = 1;
        ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 4.0F);
        ImGui::InputInt("##number", &typed);
        ImGui::SameLine();
        if (ImGui::SmallButton(label)) {
            picked = static_cast<u8>(std::clamp(typed, 1, LastLimb));
        }
        return picked;
    }
    if (ImGui::BeginCombo("##limb", label)) {
        for (usize i = 0; i < limbs->size(); ++i) {
            const auto number = static_cast<u8>(i + 1U);
            if (std::ranges::contains(taken, number, &LimbOverride::limb)) {
                continue;
            }
            const std::string text = std::format("{}  {}{}", number, (*limbs)[i].name,
                                                 (*limbs)[i].draws ? "" : "  (draws nothing)");
            if (ImGui::Selectable(text.c_str())) {
                picked = number;
            }
        }
        ImGui::EndCombo();
    }
    return picked;
}

} // namespace

bool editLimbs(std::vector<LimbOverride>& limbs, const std::string& skeleton,
               const std::optional<std::string>& object, const EditContext& context) {
    const std::vector<CatalogLimb>* catalog = findLimbs(skeleton, object, context);
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < limbs.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        isChanged = editDrawn(limbs[i], object, catalog, context) || isChanged;
        core::drawPropertyLabel("");
        if (ImGui::SmallButton("Draw its own")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        limbs.erase(limbs.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    core::drawPropertyLabel("Limbs");
    ImGui::PushID("add limb");
    if (const std::optional<u8> picked = pickLimb("+ Limb", catalog, limbs)) {
        limbs.push_back(LimbOverride{.limb = *picked});
        isChanged = true;
    }
    ImGui::PopID();
    ImGui::SetItemTooltip("Draw a limb as something other than its own display list");
    return isChanged;
}

bool editLimbPlacement(std::optional<u8>& limb, const std::string& skeleton,
                       const std::optional<std::string>& object, const EditContext& context) {
    const std::vector<CatalogLimb>* catalog = findLimbs(skeleton, object, context);
    core::drawPropertyLabel("At limb");
    ImGui::PushID("at limb");
    bool isChanged = false;
    const std::string shown = limb ? makeLimbLabel(*limb, catalog) : std::string("The actor");
    if (catalog == nullptr) {
        int typed = limb.value_or(0);
        if (ImGui::InputInt("##number", &typed)) {
            limb = typed <= 0 ? std::nullopt
                              : std::optional(static_cast<u8>(std::min(typed, LastLimb)));
            isChanged = true;
        }
    } else if (ImGui::BeginCombo("##limb", shown.c_str())) {
        if (ImGui::Selectable("The actor", !limb)) {
            isChanged = limb.has_value();
            limb.reset();
        }
        for (usize i = 0; i < catalog->size(); ++i) {
            const auto number = static_cast<u8>(i + 1U);
            if (ImGui::Selectable(makeLimbLabel(number, catalog).c_str(), limb == number)) {
                isChanged = limb != number;
                limb = number;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    ImGui::SetItemTooltip("Placed relative to a limb of the variant's first skeleton, as "
                          "PostLimbDraw draws at one, or to the actor");
    return isChanged;
}

} // namespace toki::actor
