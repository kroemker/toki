#include "PartsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/ItemRow.h"
#include "app/actor/ui/LimbsEditor.h"
#include "app/actor/ui/SegmentsEditor.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array SkeletonKinds{object::AssetKind::Skeleton};
constexpr std::array PoseKinds{object::AssetKind::Animation};
constexpr std::array DisplayListKinds{object::AssetKind::DisplayList};
constexpr int LastSetup = 70;
constexpr u8 DefaultSetup = 25;
constexpr ImVec4 PassColor{0.561F, 0.827F, 1.0F, 1.0F};
constexpr ImVec4 ShineColor{1.0F, 0.831F, 0.475F, 1.0F};
constexpr ImVec4 FacingColor{0.710F, 0.894F, 0.549F, 1.0F};
constexpr ImVec4 ItemColor{1.0F, 0.569F, 0.478F, 1.0F};
constexpr std::string_view DrawIdCollection = "GetItemDrawID";

[[nodiscard]] bool isZero(const std::array<f32, 3>& value) {
    return std::ranges::all_of(value, [](const f32 component) { return component == 0.0F; });
}

[[nodiscard]] bool isOne(const std::array<f32, 3>& value) {
    return std::ranges::all_of(value, [](const f32 component) { return component == 1.0F; });
}

[[nodiscard]] bool editSetup(std::optional<u8>& setup) {
    core::drawPropertyLabel("Setup");
    int index = setup.value_or(DefaultSetup);
    if (!ImGui::InputInt("##setup", &index)) {
        return false;
    }
    index = std::clamp(index, 0, LastSetup);
    setup = index == DefaultSetup ? std::nullopt : std::optional(static_cast<u8>(index));
    return true;
}

[[nodiscard]] std::vector<std::string> listDrawIds(const EditContext& context) {
    std::vector<std::string> names;
    if (const ValueCollection* drawIds = context.findCollection(DrawIdCollection)) {
        for (const ChoiceValue& value : drawIds->values) {
            names.push_back(value.label);
        }
    }
    return names;
}

[[nodiscard]] bool editItem(ActorPart& part, const std::span<const ActorParam> params,
                            const EditContext& context) {
    core::drawPropertyLabel("Item");
    bool isChanged = false;
    const bool isPicked = part.itemChoice.has_value();
    if (ImGui::BeginCombo("##item", isPicked ? "Picked by a param" : "One get-item model")) {
        if (ImGui::Selectable("One get-item model", !isPicked) && isPicked) {
            part.itemChoice.reset();
            isChanged = true;
        }
        if (ImGui::Selectable("Picked by a param", isPicked) && !isPicked) {
            part.itemChoice =
                ItemChoice{.param = params.empty() ? std::string() : params.front().name};
            isChanged = true;
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Drawn as GetItem_Draw draws a GetItemDrawID, from the object the "
                          "get item loads");
    if (!part.itemChoice) {
        isChanged = editPicked("Draw ID", part.symbol, listDrawIds(context), nullptr) || isChanged;
    } else {
        std::vector<std::string> names;
        for (const ActorParam& param : params) {
            names.push_back(param.name);
        }
        isChanged = editPicked("Param", part.itemChoice->param, names, nullptr) || isChanged;
        std::string table = part.itemChoice->table.value_or("");
        if (editPicked("Table", table, {}, "The param's value is the draw ID")) {
            part.itemChoice->table = table.empty() ? std::nullopt : std::optional(table);
            isChanged = true;
        }
        ImGui::SetItemTooltip("The overlay's table the param indexes, as sDrawItemIndices, "
                              "whose entries are draw IDs");
    }
    isChanged = editObject("From object", part.object, context, "the get item's") || isChanged;
    return isChanged;
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
    ImGui::SetItemTooltip("It stands in the animation at the frame below");
    core::drawPropertyLabel("At frame");
    int frame = part.poseFrame;
    if (ImGui::InputInt("##frame", &frame)) {
        part.poseFrame = static_cast<u16>(std::clamp(frame, 0, 0xFFFF));
        isChanged = true;
    }
    ImGui::SetItemTooltip("The frame of the animation it stands in, from 0");
    isChanged =
        editObject("Pose object", part.poseObject, context, skeletonObject.value_or("none")) ||
        isChanged;
    return isChanged;
}

[[nodiscard]] bool editPart(ActorPart& part, const std::optional<std::string>& object,
                            const ActorPart* skeleton, const std::span<const ActorParam> params,
                            const EditContext& context) {
    if (!core::beginProperties("part")) {
        return false;
    }
    bool isChanged = part.kind == PartKind::Item ? editItem(part, params, context)
                                                 : editSymbolOf(part, object, context);
    if (part.kind == PartKind::Skeleton) {
        isChanged = editPose(part, object, context) || isChanged;
    } else if (part.kind == PartKind::DisplayList && (skeleton != nullptr || part.limb)) {
        const std::optional<std::string>& skeletonObject =
            skeleton != nullptr && skeleton->object ? skeleton->object : object;
        isChanged = editLimbPlacement(part.limb, skeleton != nullptr ? skeleton->symbol : "",
                                      skeletonObject, context) ||
                    isChanged;
    }
    isChanged = editVector("At", part.placement.translation) || isChanged;
    isChanged = editVector("Turned", part.placement.rotation) || isChanged;
    isChanged = editVector("Scaled", part.placement.scale) || isChanged;
    core::drawPropertyLabel("Faces the camera");
    isChanged = ImGui::Checkbox("##billboard", &part.isBillboard) || isChanged;
    isChanged = editOptionalColor("Primitive", part.colors.primitive) || isChanged;
    isChanged = editOptionalColor("Environment", part.colors.environment) || isChanged;
    isChanged = editName("Pass", part.pass, PassNames) || isChanged;
    isChanged = editSetup(part.setup) || isChanged;
    core::drawPropertyLabel("Hilite");
    isChanged = ImGui::Checkbox("##hilite", &part.isHilit) || isChanged;
    ImGui::SetItemTooltip("Drawn with a LookAt from the camera toward the actor, for what shines");
    core::endProperties();
    if (part.kind == PartKind::Skeleton) {
        isChanged =
            editLimbs(part.limbs, part.symbol, part.object ? part.object : object, context) ||
            isChanged;
    }
    const std::string ownSkeleton = part.kind == PartKind::Skeleton ? part.symbol : std::string();
    isChanged =
        editSegments(part.segments, part.object ? part.object : object, ownSkeleton, context) ||
        isChanged;
    return isChanged;
}

[[nodiscard]] std::vector<RowBadge> makeBadges(const ActorPart& part) {
    std::vector<RowBadge> badges;
    if (part.pose) {
        badges.push_back(RowBadge{.text = part.poseFrame != 0
                                              ? std::format("posed, frame {}", part.poseFrame)
                                              : std::string("posed"),
                                  .color = core::palette::TextMuted});
    }
    if (!part.limbs.empty()) {
        badges.push_back(
            RowBadge{.text = std::format("limbs {}", part.limbs.size()), .color = LimbColor});
    }
    if (part.limb) {
        badges.push_back(
            RowBadge{.text = std::format("at limb {}", *part.limb), .color = LimbColor});
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
    std::ranges::move(makeSegmentBadges(part.segments), std::back_inserter(badges));
    return badges;
}

[[nodiscard]] std::string makeRowName(const ActorPart& part) {
    if (part.itemChoice) {
        return part.itemChoice->table
                   ? std::format("{}[{}]", *part.itemChoice->table, part.itemChoice->param)
                   : std::format("by {}", part.itemChoice->param);
    }
    return part.symbol.empty() ? std::string("(pick a symbol)") : part.symbol;
}

[[nodiscard]] ItemRow makeRow(const ActorPart& part, const bool isFirst) {
    const bool isList = part.kind == PartKind::DisplayList;
    const bool isItem = part.kind == PartKind::Item;
    return ItemRow{
        .tag = isItem   ? "GI"
               : isList ? "DL"
                        : "SK",
        .tagColor = isItem   ? ItemColor
                    : isList ? core::palette::Success
                             : core::palette::Accent,
        .name = makeRowName(part),
        .detail = part.object.value_or(""),
        .badges = makeBadges(part),
        .canRaise = !isFirst,
        .canCopy = true,
        .canRemove = true,
    };
}

} // namespace

bool drawPartsEditor(std::vector<ActorPart>& parts, const std::optional<std::string>& object,
                     const std::span<const ActorParam> params, const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    std::optional<usize> copied;
    std::optional<usize> raised;
    const auto skeleton = std::ranges::find(parts, PartKind::Skeleton, &ActorPart::kind);
    const ActorPart* firstSkeleton = skeleton != parts.end() ? &*skeleton : nullptr;
    for (usize i = 0; i < parts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool isAttached = parts[i].limb.has_value();
        if (isAttached) {
            ImGui::Indent();
        }
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
            isChanged = editPart(parts[i], object, firstSkeleton, params, context) || isChanged;
            endRowDetails();
        }
        if (isAttached) {
            ImGui::Unindent();
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
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Item")) {
        parts.push_back(ActorPart{.kind = PartKind::Item});
        isChanged = true;
    }
    ImGui::SetItemTooltip("A get-item model, drawn as GetItem_Draw draws it");
    return isChanged;
}

} // namespace toki::actor
