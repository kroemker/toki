#include "VariantsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/ConditionEditor.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"
#include "app/actor/ui/PartsEditor.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr f32 DefaultScale = 0.01F;
constexpr float BandWidth = 5.0F;
constexpr float HeaderButtons = 3.0F;
constexpr float HeaderArrow = 3.0F;
constexpr ImVec4 WhenColor = core::palette::Warning;
constexpr ImVec4 WhenBackground{0.910F, 0.718F, 0.314F, 0.07F};
constexpr ImVec4 DrawsColor = core::palette::Success;
constexpr ImVec4 SpawnsColor{0.780F, 0.573F, 0.918F, 1.0F};

[[nodiscard]] bool isZero(const std::array<f32, 3>& value) {
    return std::ranges::all_of(value, [](const f32 component) { return component == 0.0F; });
}

void drawBand(const char* label, const ImVec4& color, const std::optional<ImVec4>& background) {
    ImGui::TableNextRow();
    if (background) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(*background));
    }
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(color, "%s", label);
    ImGui::TableNextColumn();
}

[[nodiscard]] bool editLooks(ActorEntry& entry, ActorVariant& variant, const EditContext& context) {
    if (!beginProperties("looks")) {
        return false;
    }
    bool isChanged = editObject("Object", variant.object, context, entry.object.value_or("none"));
    isChanged =
        editOptionalNumber("Scale", variant.scale, entry.scale.value_or(DefaultScale)) || isChanged;
    isChanged = editOptionalNumber("Height offset", variant.heightOffset,
                                   entry.heightOffset.value_or(0.0F)) ||
                isChanged;
    isChanged =
        editOptionalName("Draw as", variant.draw, DrawKindNames, "Same as entry") || isChanged;
    u16 params = variant.defaultParams.value_or(0);
    if (editHex("Placed new with", params)) {
        variant.defaultParams = params;
        isChanged = true;
    }
    endProperties();
    return isChanged;
}

[[nodiscard]] ItemRow makeLooksRow(const ActorEntry& entry, const ActorVariant& variant) {
    std::vector<RowBadge> badges;
    const f32 scale = variant.scale.value_or(entry.scale.value_or(DefaultScale));
    badges.push_back(
        RowBadge{.text = std::format("scale {:g}", scale), .color = core::palette::TextMuted});
    if (const f32 height = variant.heightOffset.value_or(entry.heightOffset.value_or(0.0F));
        height != 0.0F) {
        badges.push_back(RowBadge{.text = std::format("raised {:g}", height),
                                  .color = core::palette::TextMuted});
    }
    if (variant.draw) {
        badges.push_back(RowBadge{.text = std::string(getEntryLabel(DrawKindNames, *variant.draw)),
                                  .color = core::palette::Warning});
    }
    if (variant.defaultParams) {
        badges.push_back(RowBadge{.text = std::format("new {:04X}", *variant.defaultParams),
                                  .color = core::palette::TextMuted});
    }
    const std::optional<std::string>& object = variant.object ? variant.object : entry.object;
    return ItemRow{.name = object.value_or("no object"),
                   .detail = variant.object ? std::string() : std::string("as the entry"),
                   .badges = std::move(badges)};
}

[[nodiscard]] bool drawLooks(ActorEntry& entry, ActorVariant& variant, const EditContext& context) {
    if (!drawItemRow("##looks", makeLooksRow(entry, variant)).isOpen) {
        return false;
    }
    beginRowDetails();
    const bool isChanged = editLooks(entry, variant, context);
    endRowDetails();
    return isChanged;
}

[[nodiscard]] bool editChild(ChildActor& child, const EditContext& context) {
    if (!beginProperties("child")) {
        return false;
    }
    bool isChanged = editActor("Actor", child.actor, context);
    isChanged = editHex("Params", child.params) || isChanged;
    isChanged = editVector("Offset", child.offset) || isChanged;
    endProperties();
    return isChanged;
}

[[nodiscard]] ItemRow makeChildRow(const ChildActor& child) {
    std::vector<RowBadge> badges;
    if (!isZero(child.offset)) {
        badges.push_back(RowBadge{.text = std::format("{:g}, {:g}, {:g}", child.offset[0],
                                                      child.offset[1], child.offset[2]),
                                  .color = core::palette::TextMuted});
    }
    return ItemRow{.tag = "A",
                   .tagColor = SpawnsColor,
                   .name = child.actor.empty() ? std::string("(pick an actor)") : child.actor,
                   .detail = std::format("{:04X}", child.params),
                   .badges = std::move(badges),
                   .canRemove = true};
}

[[nodiscard]] bool drawChildren(std::vector<ChildActor>& children, const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < children.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const RowResult row = drawItemRow("##child", makeChildRow(children[i]));
        if (row.action == RowAction::Remove) {
            removed = i;
        }
        if (row.isOpen) {
            beginRowDetails();
            isChanged = editChild(children[i], context) || isChanged;
            endRowDetails();
        }
        ImGui::PopID();
    }
    if (removed) {
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool drawDraws(ActorEntry& entry, ActorVariant& variant, const EditContext& context) {
    ImGui::PushID("looks");
    bool isChanged = drawLooks(entry, variant, context);
    ImGui::PopID();
    ImGui::PushID("parts");
    isChanged =
        drawPartsEditor(variant.parts, variant.object ? variant.object : entry.object, context) ||
        isChanged;
    ImGui::PopID();
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Child")) {
        variant.children.emplace_back();
        isChanged = true;
    }
    ImGui::SetItemTooltip("An actor this one spawns, drawn with it");
    return isChanged;
}

[[nodiscard]] bool drawBands(ActorEntry& entry, ActorVariant& variant, const EditContext& context) {
    if (!ImGui::BeginTable("bands", 2,
                           ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH)) {
        return false;
    }
    ImGui::TableSetupColumn("band", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::GetFontSize() * BandWidth);
    ImGui::TableSetupColumn("body", ImGuiTableColumnFlags_WidthStretch);
    drawBand("WHEN", WhenColor, WhenBackground);
    ImGui::PushID("condition");
    bool isChanged = drawConditionEditor(variant.match, entry.params, context.findCollection);
    ImGui::PopID();
    drawBand("DRAWS", DrawsColor, std::nullopt);
    isChanged = drawDraws(entry, variant, context) || isChanged;
    if (!variant.children.empty()) {
        drawBand("SPAWNS", SpawnsColor, std::nullopt);
        ImGui::PushID("children");
        isChanged = drawChildren(variant.children, context) || isChanged;
        ImGui::PopID();
    }
    ImGui::EndTable();
    return isChanged;
}

[[nodiscard]] std::string makeHeader(const ActorEntry& entry, const usize index,
                                     const EditContext& context) {
    const ActorVariant& variant = entry.variants[index];
    if (!variant.match.empty()) {
        return describeCondition(variant.match, entry.params, context.findCollection);
    }
    return index > 0 && index + 1 == entry.variants.size() ? "Otherwise" : "Every placement";
}

} // namespace

bool drawVariantsEditor(ActorEntry& entry, const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    std::optional<usize> copied;
    std::optional<usize> raised;
    for (usize i = 0; i < entry.variants.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const float buttons = ImGui::GetFrameHeight() * HeaderButtons;
        const std::string header = makeHeader(entry, i, context);
        const std::string fitted =
            makeFittedText(header, ImGui::GetContentRegionAvail().x - buttons -
                                       (ImGui::GetFontSize() * HeaderArrow));
        const std::string label = std::format("{}.  {}###variant", i + 1, fitted);
        const bool isOpen = ImGui::CollapsingHeader(
            label.c_str(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DefaultOpen);
        if (fitted != header) {
            ImGui::SetItemTooltip("%s", header.c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttons);
        if (i > 0 && drawIconButton(core::icons::ArrowUp, "Try it earlier")) {
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
            isChanged = drawBands(entry, entry.variants[i], context) || isChanged;
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    if (raised) {
        std::swap(entry.variants[*raised - 1], entry.variants[*raised]);
        isChanged = true;
    } else if (copied) {
        const ActorVariant copy = entry.variants[*copied];
        entry.variants.insert(entry.variants.begin() + static_cast<std::ptrdiff_t>(*copied) + 1,
                              copy);
        isChanged = true;
    } else if (removed) {
        entry.variants.erase(entry.variants.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::Button("+ Variant")) {
        entry.variants.emplace_back();
        isChanged = true;
    }
    return isChanged;
}

} // namespace toki::actor
