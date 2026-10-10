#include "PartsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/Sentence.h"
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
constexpr std::array PoseKinds{object::AssetKind::Animation};
constexpr std::array DisplayListKinds{object::AssetKind::DisplayList};
constexpr std::array SegmentKinds{object::AssetKind::Texture, object::AssetKind::DisplayList,
                                  object::AssetKind::Palette};
constexpr int FirstSegment = 8;
constexpr int LastSegment = 12;
constexpr int LastSetup = 70;
constexpr u8 DefaultSetup = 25;

[[nodiscard]] std::string formatVector(const std::array<f32, 3>& value) {
    return std::format("{:g}, {:g}, {:g}", value[0], value[1], value[2]);
}

[[nodiscard]] bool isZero(const std::array<f32, 3>& value) {
    return std::ranges::all_of(value, [](const f32 component) { return component == 0.0F; });
}

[[nodiscard]] bool isOne(const std::array<f32, 3>& value) {
    return std::ranges::all_of(value, [](const f32 component) { return component == 1.0F; });
}

[[nodiscard]] std::string formatScale(const std::array<f32, 3>& scale) {
    return scale[0] == scale[1] && scale[1] == scale[2] ? std::format("{:g}", scale[0])
                                                        : formatVector(scale);
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
    drawPropertyLabel("");
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

[[nodiscard]] bool editMore(ActorPart& part, const std::optional<std::string>& object,
                            const EditContext& context) {
    if (!beginProperties("more")) {
        return false;
    }
    bool isChanged = editVector("At", part.placement.translation);
    isChanged = editVector("Turned", part.placement.rotation) || isChanged;
    isChanged = editVector("Scaled", part.placement.scale) || isChanged;
    drawPropertyLabel("Faces the camera");
    isChanged = ImGui::Checkbox("##billboard", &part.isBillboard) || isChanged;
    isChanged = editOptionalColor("Primitive", part.colors.primitive) || isChanged;
    isChanged = editOptionalColor("Environment", part.colors.environment) || isChanged;
    isChanged = editSetup(part.setup) || isChanged;
    isChanged = editName("Pass", part.pass, PassNames) || isChanged;
    drawPropertyLabel("Hilite");
    isChanged = ImGui::Checkbox("##hilite", &part.isHilit) || isChanged;
    ImGui::SetItemTooltip("Drawn with a LookAt from the camera toward the actor, for what shines");
    isChanged =
        editSegments(part.segments, part.object ? part.object : object, context) || isChanged;
    endProperties();
    return isChanged;
}

[[nodiscard]] bool editSymbolOf(ActorPart& part, const std::optional<std::string>& object,
                                const EditContext& context) {
    if (!beginProperties("symbol")) {
        return false;
    }
    bool isChanged = editObject("Object", part.object, context, object.value_or("none"));
    const bool isList = part.kind == PartKind::DisplayList;
    isChanged = editSymbol(isList ? "Display list" : "Skeleton", part.symbol,
                           part.object ? part.object : object,
                           isList ? std::span<const object::AssetKind>(DisplayListKinds)
                                  : std::span<const object::AssetKind>(SkeletonKinds),
                           context) ||
                isChanged;
    endProperties();
    return isChanged;
}

[[nodiscard]] bool editPose(ActorPart& part, const std::optional<std::string>& object,
                            const EditContext& context) {
    if (!beginProperties("pose")) {
        return false;
    }
    const std::optional<std::string>& skeletonObject = part.object ? part.object : object;
    bool isChanged =
        editObject("Object", part.poseObject, context, skeletonObject.value_or("none"));
    isChanged = editOptionalSymbol("Animation", part.pose,
                                   part.poseObject ? part.poseObject : skeletonObject, PoseKinds,
                                   context) ||
                isChanged;
    endProperties();
    ImGui::TextColored(core::palette::TextMuted, "It stands in the animation's first frame.");
    return isChanged;
}

[[nodiscard]] bool drawPart(ActorPart& part, const std::optional<std::string>& object,
                            const EditContext& context, Sentence& sentence) {
    const bool isList = part.kind == PartKind::DisplayList;
    sentence.addWords(isList ? "Display list" : "Skeleton");
    bool isChanged =
        sentence.addToken("##symbol", part.symbol.empty() ? std::string("(pick one)") : part.symbol,
                          [&] { return editSymbolOf(part, object, context); });
    if (part.object) {
        sentence.addWords("from");
        isChanged = sentence.addToken("##object", *part.object, [&] {
            return editSymbolOf(part, object, context);
        }) || isChanged;
    }
    if (!isList) {
        sentence.addWords(part.pose ? "posed as" : "at rest,");
        isChanged = sentence.addToken("##pose", part.pose.value_or("pose it"), [&] {
            return editPose(part, object, context);
        }) || isChanged;
    }
    const auto addPlacement = [&](const char* words, const char* id, const std::string& text) {
        sentence.addWords(words);
        return sentence.addToken(id, text, [&] { return editMore(part, object, context); });
    };
    if (!isZero(part.placement.translation)) {
        isChanged =
            addPlacement("at", "##at", formatVector(part.placement.translation)) || isChanged;
    }
    if (!isZero(part.placement.rotation)) {
        isChanged =
            addPlacement("turned", "##turned", formatVector(part.placement.rotation)) || isChanged;
    }
    if (!isOne(part.placement.scale)) {
        isChanged =
            addPlacement("scaled", "##scaled", formatScale(part.placement.scale)) || isChanged;
    }
    return sentence.addToken("##more", "…", [&] { return editMore(part, object, context); }) ||
           isChanged;
}

} // namespace

bool drawPartsEditor(std::vector<ActorPart>& parts, const std::optional<std::string>& object,
                     const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    std::optional<usize> copied;
    std::optional<std::pair<usize, usize>> swapped;
    for (usize i = 0; i < parts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        Sentence sentence;
        isChanged = drawPart(parts[i], object, context, sentence) || isChanged;
        if (i > 0 && sentence.addButton(core::icons::ArrowUp, "Draw it earlier")) {
            swapped = std::pair(i - 1, i);
        }
        if (sentence.addButton(core::icons::Copy, "Copy it")) {
            copied = i;
        }
        if (sentence.addButton(core::icons::Trash, "Remove it")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (swapped) {
        std::swap(parts[swapped->first], parts[swapped->second]);
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
