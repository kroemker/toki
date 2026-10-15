#include "AdjustmentsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/ConditionEditor.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"
#include "app/actor/ui/SegmentsEditor.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr f32 DefaultScale = 0.01F;
constexpr float HeaderButtons = 3.0F;
constexpr float HeaderArrow = 3.0F;
constexpr ImVec4 ChangesColor{0.561F, 0.827F, 1.0F, 1.0F};

[[nodiscard]] std::string describeChanges(const ActorAdjustment& adjustment) {
    std::vector<std::string> changes;
    if (adjustment.scale) {
        changes.push_back(std::format("scale {:g}", *adjustment.scale));
    }
    if (adjustment.heightOffset) {
        changes.push_back(std::format("raised {:g}", *adjustment.heightOffset));
    }
    if (adjustment.rotates) {
        changes.emplace_back("axes");
    }
    if (adjustment.colors.primitive || adjustment.colors.environment) {
        changes.emplace_back("colours");
    }
    if (!adjustment.segments.empty()) {
        changes.push_back(std::format("segments {}", adjustment.segments.size()));
    }
    std::string text;
    for (const std::string& change : changes) {
        text += text.empty() ? change : ", " + change;
    }
    return text.empty() ? std::string("nothing yet") : text;
}

[[nodiscard]] bool editRotates(std::optional<RotationAxes>& rotates, const RotationAxes& entry) {
    core::drawPropertyLabel("Turns by");
    bool isSet = rotates.has_value();
    bool isChanged = ImGui::Checkbox("##set", &isSet);
    if (isChanged) {
        rotates = isSet ? std::optional(entry) : std::nullopt;
    }
    ImGui::SetItemTooltip("Change which of its placed rotation's axes it turns by");
    RotationAxes shown = rotates.value_or(entry);
    ImGui::BeginDisabled(!isSet);
    for (const auto& [label, axis] :
         {std::pair("X", &RotationAxes::x), std::pair("Y", &RotationAxes::y),
          std::pair("Z", &RotationAxes::z)}) {
        ImGui::SameLine();
        if (ImGui::Checkbox(label, &(shown.*axis)) && isSet) {
            rotates = shown;
            isChanged = true;
        }
    }
    ImGui::EndDisabled();
    return isChanged;
}

[[nodiscard]] usize countParts(const ActorEntry& entry) {
    usize count = 0;
    for (const ActorVariant& variant : entry.variants) {
        count = std::max(count, variant.parts.size());
    }
    return count;
}

[[nodiscard]] bool editParts(std::vector<usize>& parts, const usize count) {
    core::drawPropertyLabel("Of parts");
    bool isChanged = false;
    bool isAll = parts.empty();
    if (ImGui::Checkbox("All", &isAll)) {
        parts = isAll ? std::vector<usize>() : std::vector<usize>{0};
        isChanged = true;
    }
    ImGui::SetItemTooltip("Which of the variant's parts its colours and segments change, "
                          "counted from 0");
    if (isAll) {
        return isChanged;
    }
    for (usize part = 0; part < std::max(count, parts.empty() ? 0 : parts.back() + 1); ++part) {
        ImGui::SameLine();
        bool isChosen = std::ranges::contains(parts, part);
        if (ImGui::Checkbox(std::format("{}##part{}", part, part).c_str(), &isChosen)) {
            if (isChosen) {
                parts.push_back(part);
                std::ranges::sort(parts);
            } else {
                std::erase(parts, part);
            }
            isChanged = true;
        }
    }
    return isChanged;
}

[[nodiscard]] bool editChanges(ActorEntry& entry, ActorAdjustment& adjustment,
                               const EditContext& context) {
    if (!core::beginProperties("changes")) {
        return false;
    }
    bool isChanged =
        editOptionalNumber("Scale", adjustment.scale, entry.scale.value_or(DefaultScale));
    isChanged = editOptionalNumber("Height offset", adjustment.heightOffset,
                                   entry.heightOffset.value_or(0.0F)) ||
                isChanged;
    isChanged = editRotates(adjustment.rotates, entry.rotates) || isChanged;
    isChanged = editOptionalColor("Primitive", adjustment.colors.primitive) || isChanged;
    isChanged = editOptionalColor("Environment", adjustment.colors.environment) || isChanged;
    isChanged = editParts(adjustment.parts, countParts(entry)) || isChanged;
    core::endProperties();
    isChanged = editSegments(adjustment.segments, entry.object, {}, context) || isChanged;
    return isChanged;
}

[[nodiscard]] bool drawBands(ActorEntry& entry, ActorAdjustment& adjustment,
                             const EditContext& context) {
    if (!beginBands()) {
        return false;
    }
    drawBand("WHEN", WhenColor, WhenBackground);
    ImGui::PushID("condition");
    bool isChanged = drawConditionEditor(adjustment.match, entry.params, context.findCollection);
    ImGui::PopID();
    drawBand("CHANGES", ChangesColor, std::nullopt);
    isChanged = editChanges(entry, adjustment, context) || isChanged;
    endBands();
    return isChanged;
}

[[nodiscard]] std::string makeHeader(const ActorEntry& entry, const ActorAdjustment& adjustment,
                                     const EditContext& context) {
    const std::string condition =
        adjustment.match.empty()
            ? std::string("Every placement")
            : describeCondition(adjustment.match, entry.params, context.findCollection);
    return std::format("{}: {}", condition, describeChanges(adjustment));
}

} // namespace

bool drawAdjustmentsEditor(ActorEntry& entry, const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    std::optional<usize> copied;
    std::optional<usize> raised;
    for (usize i = 0; i < entry.adjustments.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const float buttons = ImGui::GetFrameHeight() * HeaderButtons;
        const std::string header = makeHeader(entry, entry.adjustments[i], context);
        const std::string fitted =
            makeFittedText(header, ImGui::GetContentRegionAvail().x - buttons -
                                       (ImGui::GetFontSize() * HeaderArrow));
        const std::string label = std::format("{}.  {}###adjustment", i + 1, fitted);
        const bool isOpen = ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
        if (fitted != header) {
            ImGui::SetItemTooltip("%s", header.c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttons);
        if (i > 0 && drawIconButton(core::icons::ArrowUp, "Make it earlier")) {
            raised = i;
        }
        ImGui::SameLine();
        if (drawIconButton(core::icons::Copy, "Copy it")) {
            copied = i;
        }
        ImGui::SameLine();
        if (drawIconButton(core::icons::Trash, "Remove it")) {
            removed = i;
        }
        if (isOpen) {
            isChanged = drawBands(entry, entry.adjustments[i], context) || isChanged;
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    if (raised) {
        std::swap(entry.adjustments[*raised - 1], entry.adjustments[*raised]);
        isChanged = true;
    } else if (copied) {
        const ActorAdjustment copy = entry.adjustments[*copied];
        entry.adjustments.insert(
            entry.adjustments.begin() + static_cast<std::ptrdiff_t>(*copied) + 1, copy);
        isChanged = true;
    } else if (removed) {
        entry.adjustments.erase(entry.adjustments.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::Button("+ Adjustment")) {
        entry.adjustments.emplace_back();
        isChanged = true;
    }
    ImGui::SetItemTooltip("A change made to the variant drawn, for the placements it matches");
    return isChanged;
}

} // namespace toki::actor
