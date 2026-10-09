#include "VariantEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/DrawSteps.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/MatchEditor.h"
#include "app/actor/ui/SpawnEditor.h"
#include "app/actor/ui/StepsEditor.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/object/domain/ObjectAssets.h"

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

constexpr std::array SkeletonKinds{object::AssetKind::Skeleton};
constexpr std::array AnimationKinds{object::AssetKind::Animation,
                                    object::AssetKind::PlayerAnimation};
constexpr std::array DisplayListKinds{object::AssetKind::DisplayList};
constexpr std::array SegmentKinds{object::AssetKind::Texture, object::AssetKind::DisplayList,
                                  object::AssetKind::Palette};
constexpr int FirstSegment = 8;
constexpr int LastSegment = 12;

[[nodiscard]] bool editSegments(std::vector<SegmentSymbol>& segments,
                                const std::optional<std::string>& object,
                                const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < segments.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        int segment = segments[i].segment;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
        if (ImGui::SliderInt("##segment", &segment, FirstSegment, LastSegment)) {
            segments[i].segment = static_cast<u8>(segment);
            isChanged = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 2.0F);
        isChanged =
            editSymbol("##symbol", segments[i].name, object, SegmentKinds, context) || isChanged;
        ImGui::SameLine();
        if (drawIconButton(core::icons::Trash, "Remove this segment")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("Point a segment")) {
        segments.push_back(SegmentSymbol{.segment = static_cast<u8>(FirstSegment)});
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool editDisplayList(DisplayListPart& part, const std::optional<std::string>& object,
                                   const EditContext& context) {
    const std::optional<std::string>& partObject = part.object ? part.object : object;
    bool isChanged = editObject("Object", part.object, context, "the variant's");
    isChanged =
        editSymbol("Display list", part.name, partObject, DisplayListKinds, context) || isChanged;
    isChanged = editName("Pass", part.pass, PassNames) || isChanged;
    isChanged = editVector("Translation", part.placement.translation) || isChanged;
    isChanged = editVector("Rotation", part.placement.rotation) || isChanged;
    isChanged = editVector("Scale", part.placement.scale) || isChanged;
    bool isBillboard = part.isBillboard.value_or(false);
    if (ImGui::Checkbox("Billboard", &isBillboard)) {
        part.isBillboard = isBillboard ? std::optional(true) : std::nullopt;
        isChanged = true;
    }
    isChanged = editOptionalColor("Primitive", part.colors.primitive) || isChanged;
    isChanged = editOptionalColor("Environment", part.colors.environment) || isChanged;
    ImGui::TextColored(core::palette::TextMuted, "Its own segments");
    return editSegments(part.segments, partObject, context) || isChanged;
}

[[nodiscard]] bool editDisplayLists(std::vector<DisplayListPart>& parts,
                                    const std::optional<std::string>& object,
                                    const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < parts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::format(
            "{}###list", parts[i].name.empty() ? std::string("(no display list)") : parts[i].name);
        const bool isOpen = ImGui::TreeNode(label.c_str());
        ImGui::SameLine();
        if (drawIconButton(core::icons::Trash, "Remove this display list")) {
            removed = i;
        }
        if (isOpen) {
            isChanged = editDisplayList(parts[i], object, context) || isChanged;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removed) {
        parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("Add a display list")) {
        parts.emplace_back();
        isChanged = true;
    }
    return isChanged;
}

void clearDrawingKeys(ActorVariant& variant) {
    variant.skeleton.reset();
    variant.animation.reset();
    variant.animationObject.reset();
    variant.frame.reset();
    variant.displayLists.clear();
    variant.segments.clear();
    variant.colors = {};
    variant.isBillboard.reset();
}

[[nodiscard]] bool editKeys(ActorVariant& variant, const std::optional<std::string>& object,
                            const EditContext& context) {
    bool isChanged =
        editOptionalSymbol("Skeleton", variant.skeleton, object, SkeletonKinds, context);
    if (variant.skeleton) {
        isChanged =
            editObject("Animation object", variant.animationObject, context, "the variant's") ||
            isChanged;
        isChanged = editOptionalSymbol("Animation", variant.animation,
                                       variant.animationObject ? variant.animationObject : object,
                                       AnimationKinds, context) ||
                    isChanged;
        int frame = static_cast<int>(variant.frame.value_or(0));
        if (ImGui::InputInt("Frame", &frame)) {
            variant.frame = static_cast<u32>(std::max(frame, 0));
            isChanged = true;
        }
    } else {
        ImGui::TextColored(core::palette::TextMuted, "Display lists");
        isChanged = editDisplayLists(variant.displayLists, object, context) || isChanged;
    }
    ImGui::TextColored(core::palette::TextMuted, "Segments");
    isChanged = editSegments(variant.segments, object, context) || isChanged;
    isChanged = editOptionalColor("Primitive", variant.colors.primitive) || isChanged;
    isChanged = editOptionalColor("Environment", variant.colors.environment) || isChanged;
    bool isBillboard = variant.isBillboard.value_or(false);
    if (ImGui::Checkbox("Billboard", &isBillboard)) {
        variant.isBillboard = isBillboard ? std::optional(true) : std::nullopt;
        isChanged = true;
    }
    if (ImGui::Button("Draw it by steps instead")) {
        variant.steps = makeDrawSteps(variant);
        clearDrawingKeys(variant);
        isChanged = true;
    }
    ImGui::SetItemTooltip("For what the keys cannot say: setup display lists, limbs, "
                          "transforms that build on each other");
    return isChanged;
}

[[nodiscard]] bool editDrawing(ActorVariant& variant, const std::optional<std::string>& object,
                               const EditContext& context) {
    if (variant.steps.empty()) {
        return editKeys(variant, object, context);
    }
    bool isChanged = drawStepsEditor(variant.steps, object, context, false);
    if (ImGui::Button("Remove every step")) {
        variant.steps.clear();
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool editChildren(std::vector<ChildActor>& children, const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < children.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        isChanged = editActor("Actor", children[i].actor, context) || isChanged;
        isChanged = editHex("Params", children[i].params) || isChanged;
        isChanged = editVector("Offset", children[i].offset) || isChanged;
        if (ImGui::SmallButton("Remove this child")) {
            removed = i;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (removed) {
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("Spawn a child")) {
        children.emplace_back();
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool editVariant(ActorEntry& entry, ActorVariant& variant,
                               const EditContext& context) {
    bool isChanged = false;
    if (ImGui::TreeNodeEx("Match", ImGuiTreeNodeFlags_DefaultOpen)) {
        isChanged = drawMatchEditor(variant.match, entry.fields) || isChanged;
        ImGui::TreePop();
    }
    isChanged = editObject("Object", variant.object, context,
                           entry.object ? *entry.object : std::string("none")) ||
                isChanged;
    isChanged =
        editOptionalNumber("Scale", variant.scale, entry.scale.value_or(0.01F)) || isChanged;
    isChanged = editOptionalNumber("Height offset", variant.heightOffset,
                                   entry.heightOffset.value_or(0.0F)) ||
                isChanged;
    isChanged = editOptionalName("Draw", variant.draw, DrawKindNames, "the entry's") || isChanged;
    const std::optional<std::string>& object = variant.object ? variant.object : entry.object;
    if (ImGui::TreeNodeEx("Drawing", ImGuiTreeNodeFlags_DefaultOpen)) {
        isChanged = editDrawing(variant, object, context) || isChanged;
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Children")) {
        isChanged = editChildren(variant.children, context) || isChanged;
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Spawn and params")) {
        isChanged = drawSpawnEditor(variant.spawn, entry.fields) || isChanged;
        bool hasParams = variant.defaultParams.has_value();
        if (ImGui::Checkbox("Default params", &hasParams)) {
            variant.defaultParams = hasParams ? std::optional(u16{0}) : std::nullopt;
            isChanged = true;
        }
        if (variant.defaultParams) {
            ImGui::SameLine();
            isChanged = editHex("##params", *variant.defaultParams) || isChanged;
        }
        ImGui::TreePop();
    }
    return isChanged;
}

} // namespace

bool drawVariantsEditor(ActorEntry& entry, const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    std::optional<usize> copied;
    std::optional<std::pair<usize, usize>> swapped;
    for (usize i = 0; i < entry.variants.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label =
            std::format("{}. {}###variant", i + 1, describeMatch(entry.variants[i].match));
        const bool isOpen = ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 3.0F);
        if (i > 0 && drawIconButton(core::icons::ArrowUp, "Try it earlier")) {
            swapped = std::pair(i - 1, i);
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
            ImGui::Indent();
            isChanged = editVariant(entry, entry.variants[i], context) || isChanged;
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    if (swapped) {
        std::swap(entry.variants[swapped->first], entry.variants[swapped->second]);
        isChanged = true;
    }
    if (copied) {
        const ActorVariant copy = entry.variants[*copied];
        entry.variants.insert(entry.variants.begin() + static_cast<std::ptrdiff_t>(*copied), copy);
        isChanged = true;
    } else if (removed) {
        entry.variants.erase(entry.variants.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::Button("Add a variant")) {
        entry.variants.emplace_back();
        isChanged = true;
    }
    return isChanged;
}

} // namespace toki::actor
