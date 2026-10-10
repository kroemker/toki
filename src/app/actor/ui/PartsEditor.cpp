#include "PartsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"
#include "app/core/ui/Theme.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array SkeletonKinds{object::AssetKind::Skeleton};
constexpr std::array PoseKinds{object::AssetKind::Animation};
constexpr std::array DisplayListKinds{object::AssetKind::DisplayList};
constexpr std::array SegmentKinds{object::AssetKind::Texture, object::AssetKind::DisplayList,
                                  object::AssetKind::Palette};
constexpr int FirstSegment = 8;
constexpr int LastSegment = 12;
constexpr int LastSetup = 70;
constexpr u8 DefaultSetup = 25;
constexpr ImVec4 PassColor{0.561F, 0.827F, 1.0F, 1.0F};
constexpr ImVec4 ShineColor{1.0F, 0.831F, 0.475F, 1.0F};
constexpr ImVec4 FacingColor{0.710F, 0.894F, 0.549F, 1.0F};

[[nodiscard]] bool isZero(const std::array<f32, 3>& value) {
    return std::ranges::all_of(value, [](const f32 component) { return component == 0.0F; });
}

[[nodiscard]] bool isOne(const std::array<f32, 3>& value) {
    return std::ranges::all_of(value, [](const f32 component) { return component == 1.0F; });
}

[[nodiscard]] bool editSegments(std::vector<SegmentSymbol>& segments,
                                const std::optional<std::string>& object,
                                const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < segments.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::format("Segment {}", segments[i].segment);
        isChanged =
            editSymbol(label.c_str(), segments[i].name, object, SegmentKinds, context) || isChanged;
        drawPropertyLabel("");
        if (ImGui::SmallButton(
                std::format("Point segment {} at nothing", segments[i].segment).c_str())) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    drawPropertyLabel("Segments");
    for (int segment = FirstSegment; segment <= LastSegment; ++segment) {
        if (std::ranges::contains(segments, static_cast<u8>(segment), &SegmentSymbol::segment)) {
            continue;
        }
        const std::string button = std::format("+ {}", segment);
        if (ImGui::SmallButton(button.c_str())) {
            segments.push_back(SegmentSymbol{.segment = static_cast<u8>(segment)});
            isChanged = true;
        }
        ImGui::SetItemTooltip("Point segment %d at a symbol", segment);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    return isChanged;
}

[[nodiscard]] bool editSetup(std::optional<u8>& setup) {
    drawPropertyLabel("Setup");
    int index = setup.value_or(DefaultSetup);
    if (!ImGui::InputInt("##setup", &index)) {
        return false;
    }
    index = std::clamp(index, 0, LastSetup);
    setup = index == DefaultSetup ? std::nullopt : std::optional(static_cast<u8>(index));
    return true;
}

[[nodiscard]] bool editSymbolOf(ActorPart& part, const std::optional<std::string>& object,
                                const EditContext& context) {
    const bool isList = part.kind == PartKind::DisplayList;
    bool isChanged = editSymbol(isList ? "Display list" : "Skeleton", part.symbol,
                                part.object ? part.object : object,
                                isList ? std::span<const object::AssetKind>(DisplayListKinds)
                                       : std::span<const object::AssetKind>(SkeletonKinds),
                                context);
    isChanged =
        editObject("From object", part.object, context, object.value_or("none")) || isChanged;
    return isChanged;
}

[[nodiscard]] bool editPose(ActorPart& part, const std::optional<std::string>& object,
                            const EditContext& context) {
    const std::optional<std::string>& skeletonObject = part.object ? part.object : object;
    bool isChanged =
        editOptionalSymbol("Posed as", part.pose,
                           part.poseObject ? part.poseObject : skeletonObject, PoseKinds, context);
    ImGui::SetItemTooltip("It stands in the animation's first frame");
    isChanged =
        editObject("Pose object", part.poseObject, context, skeletonObject.value_or("none")) ||
        isChanged;
    return isChanged;
}

[[nodiscard]] bool editPart(ActorPart& part, const std::optional<std::string>& object,
                            const EditContext& context) {
    if (!beginProperties("part")) {
        return false;
    }
    bool isChanged = editSymbolOf(part, object, context);
    if (part.kind == PartKind::Skeleton) {
        isChanged = editPose(part, object, context) || isChanged;
    }
    isChanged = editVector("At", part.placement.translation) || isChanged;
    isChanged = editVector("Turned", part.placement.rotation) || isChanged;
    isChanged = editVector("Scaled", part.placement.scale) || isChanged;
    drawPropertyLabel("Faces the camera");
    isChanged = ImGui::Checkbox("##billboard", &part.isBillboard) || isChanged;
    isChanged = editOptionalColor("Primitive", part.colors.primitive) || isChanged;
    isChanged = editOptionalColor("Environment", part.colors.environment) || isChanged;
    isChanged = editName("Pass", part.pass, PassNames) || isChanged;
    isChanged = editSetup(part.setup) || isChanged;
    drawPropertyLabel("Hilite");
    isChanged = ImGui::Checkbox("##hilite", &part.isHilit) || isChanged;
    ImGui::SetItemTooltip("Drawn with a LookAt from the camera toward the actor, for what shines");
    isChanged =
        editSegments(part.segments, part.object ? part.object : object, context) || isChanged;
    endProperties();
    return isChanged;
}

[[nodiscard]] std::vector<RowBadge> makeBadges(const ActorPart& part) {
    std::vector<RowBadge> badges;
    if (part.pose) {
        badges.push_back(RowBadge{.text = "posed", .color = core::palette::TextMuted});
    }
    if (!isZero(part.placement.translation)) {
        badges.push_back(RowBadge{.text = "moved", .color = core::palette::TextMuted});
    }
    if (!isZero(part.placement.rotation)) {
        badges.push_back(RowBadge{.text = "turned", .color = core::palette::TextMuted});
    }
    if (!isOne(part.placement.scale)) {
        badges.push_back(RowBadge{.text = "scaled", .color = core::palette::TextMuted});
    }
    if (part.isBillboard) {
        badges.push_back(RowBadge{.text = "faces cam", .color = FacingColor});
    }
    if (part.pass == Pass::Translucent) {
        badges.push_back(RowBadge{.text = "xlu", .color = PassColor});
    }
    if (part.isHilit) {
        badges.push_back(RowBadge{.text = "shiny", .color = ShineColor});
    }
    if (part.setup) {
        badges.push_back(RowBadge{.text = std::format("setup {}", *part.setup),
                                  .color = core::palette::TextMuted});
    }
    if (part.colors.primitive || part.colors.environment) {
        badges.push_back(RowBadge{.text = "colours", .color = core::palette::TextMuted});
    }
    if (!part.segments.empty()) {
        badges.push_back(RowBadge{.text = std::format("segments {}", part.segments.size()),
                                  .color = core::palette::TextMuted});
    }
    return badges;
}

[[nodiscard]] ItemRow makeRow(const ActorPart& part, const bool isFirst) {
    const bool isList = part.kind == PartKind::DisplayList;
    return ItemRow{
        .tag = isList ? "DL" : "SK",
        .tagColor = isList ? core::palette::Success : core::palette::Accent,
        .name = part.symbol.empty() ? std::string("(pick a symbol)") : part.symbol,
        .detail = part.object.value_or(""),
        .badges = makeBadges(part),
        .canRaise = !isFirst,
        .canCopy = true,
        .canRemove = true,
    };
}

} // namespace

bool drawPartsEditor(std::vector<ActorPart>& parts, const std::optional<std::string>& object,
                     const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    std::optional<usize> copied;
    std::optional<usize> raised;
    for (usize i = 0; i < parts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const RowResult row = drawItemRow("##part", makeRow(parts[i], i == 0));
        if (row.action == RowAction::Raise) {
            raised = i;
        } else if (row.action == RowAction::Copy) {
            copied = i;
        } else if (row.action == RowAction::Remove) {
            removed = i;
        }
        if (row.isOpen) {
            beginRowDetails();
            isChanged = editPart(parts[i], object, context) || isChanged;
            endRowDetails();
        }
        ImGui::PopID();
    }
    if (raised) {
        std::swap(parts[*raised - 1], parts[*raised]);
        isChanged = true;
    } else if (copied) {
        const ActorPart copy = parts[*copied];
        parts.insert(parts.begin() + static_cast<std::ptrdiff_t>(*copied) + 1, copy);
        isChanged = true;
    } else if (removed) {
        parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("+ Display list")) {
        parts.push_back(ActorPart{.kind = PartKind::DisplayList});
        isChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Skeleton")) {
        parts.push_back(ActorPart{.kind = PartKind::Skeleton});
        isChanged = true;
    }
    return isChanged;
}

} // namespace toki::actor
