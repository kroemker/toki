#include "LimbsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"
#include "app/core/ui/PropertyTable.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
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

[[nodiscard]] std::optional<std::string> findLimbName(const u8 limb,
                                                      const std::vector<CatalogLimb>* limbs) {
    const usize index = limb - 1U;
    if (limbs == nullptr || index >= limbs->size() || (*limbs)[index].name.empty()) {
        return std::nullopt;
    }
    return (*limbs)[index].name;
}

[[nodiscard]] std::string makeLimbLabel(const u8 limb, const std::vector<CatalogLimb>* limbs) {
    const std::optional<std::string> name = findLimbName(limb, limbs);
    return name ? std::format("{}  {}", limb, *name) : std::format("Limb {}", limb);
}

[[nodiscard]] std::string makeRowId(const u8 limb) {
    return std::format("limb {}", limb);
}

[[nodiscard]] ItemRow makeLimbRow(const LimbOverride& limb, const std::vector<CatalogLimb>* limbs) {
    std::string detail = "draws nothing";
    if (limb.displayList) {
        detail = "draws " + (limb.displayList->empty() ? std::string("…") : *limb.displayList);
        if (limb.object) {
            detail += std::format(" of {}", *limb.object);
        }
    }
    return ItemRow{.tag = std::to_string(limb.limb),
                   .tagColor = LimbColor,
                   .name = findLimbName(limb.limb, limbs).value_or("Limb"),
                   .detail = detail,
                   .canRemove = true};
}

void openRow(const u8 limb) {
    ImGui::PushID(makeRowId(limb).c_str());
    ImGui::GetStateStorage()->SetBool(ImGui::GetID("##open"), true);
    ImGui::PopID();
}

[[nodiscard]] bool editDrawn(LimbOverride& limb, const std::optional<std::string>& object,
                             const EditContext& context) {
    core::drawPropertyLabel("Draws");
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
    ImGui::SetNextItemWidth(-FLT_MIN);
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
    ImGui::PushID("limbs");
    ImGui::SeparatorText("Limbs");
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < limbs.size(); ++i) {
        const RowResult row =
            drawItemRow(makeRowId(limbs[i].limb).c_str(), makeLimbRow(limbs[i], catalog));
        if (row.action == RowAction::Remove) {
            removed = i;
        }
        if (!row.isOpen) {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));
        beginRowDetails();
        if (core::beginProperties("limb")) {
            isChanged = editDrawn(limbs[i], object, context) || isChanged;
            core::endProperties();
        }
        endRowDetails();
        ImGui::PopID();
    }
    if (removed) {
        limbs.erase(limbs.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    ImGui::PushID("add limb");
    if (const std::optional<u8> picked = pickLimb("+ Limb", catalog, limbs)) {
        limbs.push_back(LimbOverride{.limb = *picked});
        openRow(*picked);
        isChanged = true;
    }
    ImGui::PopID();
    ImGui::SetItemTooltip("Draw a limb as something other than its own display list");
    ImGui::PopID();
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
