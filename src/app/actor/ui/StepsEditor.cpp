#include "StepsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"
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
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array SkeletonKinds{object::AssetKind::Skeleton};
constexpr std::array AnimationKinds{object::AssetKind::Animation,
                                    object::AssetKind::PlayerAnimation};
constexpr std::array DisplayListKinds{object::AssetKind::DisplayList};
constexpr std::array SegmentKinds{object::AssetKind::Texture, object::AssetKind::DisplayList,
                                  object::AssetKind::Palette};
constexpr int LastSetup = 70;
constexpr int FirstSegment = 8;
constexpr int LastSegment = 12;

[[nodiscard]] const char* getStepName(const DrawStep& step) {
    constexpr std::array Names{"Setup",     "Colours",      "Hilite",   "Segment",
                               "Transform", "Display list", "Skeleton", "Shadow"};
    return Names[step.step.index()];
}

[[nodiscard]] DrawStep makeStep(const usize kind) {
    switch (kind) {
    case 0:
        return DrawStep{.step = SetupStep{.index = 25}};
    case 1:
        return DrawStep{.step = ColorsStep{}};
    case 2:
        return DrawStep{.step = HiliteStep{}};
    case 3:
        return DrawStep{.step = SegmentStep{.segment = 8}};
    case 4:
        return DrawStep{.step = TransformStep{}};
    case 5:
        return DrawStep{.step = DisplayListStep{}};
    case 6:
        return DrawStep{.step = SkeletonStep{}};
    default:
        return DrawStep{.step = ShadowStep{.shadow = {.kind = ShadowKind::Circle, .size = 10.0F}}};
    }
}

[[nodiscard]] bool editPlacement(PartPlacement& placement) {
    bool isChanged = editVector("Translation", placement.translation);
    isChanged = editVector("Rotation", placement.rotation) || isChanged;
    return editVector("Scale", placement.scale) || isChanged;
}

[[nodiscard]] bool editShadow(Shadow& shadow) {
    bool isChanged = editName("Kind", shadow.kind, ShadowKindNames);
    isChanged = ImGui::DragFloat("Size", &shadow.size, 0.1F, 0.0F, 0.0F, "%.4g") || isChanged;
    int alpha = shadow.alpha;
    if (ImGui::SliderInt("Alpha", &alpha, 0, 255)) {
        shadow.alpha = static_cast<u8>(alpha);
        isChanged = true;
    }
    return isChanged;
}

class StepEditor {
public:
    StepEditor(const std::optional<std::string>& object, const EditContext& context)
        : object(object), context(context) {}

    [[nodiscard]] bool edit(SetupStep& step) const {
        int index = step.index;
        if (!ImGui::SliderInt("sSetupDL", &index, 0, LastSetup)) {
            return false;
        }
        step.index = static_cast<u8>(index);
        return true;
    }

    [[nodiscard]] bool edit(ColorsStep& step) const {
        const bool isChanged = editOptionalColor("Primitive", step.colors.primitive);
        return editOptionalColor("Environment", step.colors.environment) || isChanged;
    }

    [[nodiscard]] bool edit(HiliteStep&) const {
        ImGui::TextColored(core::palette::TextMuted,
                           "What follows shines, lit from the camera toward the actor");
        return false;
    }

    [[nodiscard]] bool edit(SegmentStep& step) const {
        int segment = step.segment;
        bool isChanged = false;
        if (ImGui::SliderInt("Segment", &segment, FirstSegment, LastSegment)) {
            step.segment = static_cast<u8>(segment);
            isChanged = true;
        }
        isChanged = editObject("Object", step.object, context, "the variant's") || isChanged;
        return editSymbol("Symbol", step.symbol, step.object ? step.object : object, SegmentKinds,
                          context) ||
               isChanged;
    }

    [[nodiscard]] bool edit(TransformStep& step) const {
        bool isChanged = editPlacement(step.placement);
        isChanged = ImGui::Checkbox("Billboard", &step.isBillboard) || isChanged;
        ImGui::SameLine();
        isChanged = ImGui::Checkbox("From the actor's matrix again", &step.resets) || isChanged;
        return isChanged;
    }

    [[nodiscard]] bool edit(DisplayListStep& step) const {
        bool isChanged = editObject("Object", step.object, context, "the variant's");
        isChanged = editSymbol("Display list", step.name, step.object ? step.object : object,
                               DisplayListKinds, context) ||
                    isChanged;
        return editName("Pass", step.pass, PassNames) || isChanged;
    }

    [[nodiscard]] bool edit(SkeletonStep& step) const;

    [[nodiscard]] bool edit(ShadowStep& step) const {
        return editShadow(step.shadow);
    }

private:
    [[nodiscard]] bool editLimbs(SkeletonStep& step) const;

    const std::optional<std::string>& object;
    const EditContext& context;
};

bool StepEditor::edit(SkeletonStep& step) const {
    const std::optional<std::string>& skeletonObject = step.object ? step.object : object;
    bool isChanged = editObject("Object", step.object, context, "the variant's");
    isChanged =
        editSymbol("Skeleton", step.skeleton, skeletonObject, SkeletonKinds, context) || isChanged;
    isChanged = editObject("Animation object", step.animationObject, context, "the skeleton's") ||
                isChanged;
    isChanged = editOptionalSymbol("Animation", step.animation,
                                   step.animationObject ? step.animationObject : skeletonObject,
                                   AnimationKinds, context) ||
                isChanged;
    int frame = static_cast<int>(step.frame.value_or(0));
    if (ImGui::InputInt("Frame", &frame)) {
        step.frame = static_cast<u32>(std::max(frame, 0));
        isChanged = true;
    }
    isChanged = editName("Pass", step.pass, PassNames) || isChanged;
    return editLimbs(step) || isChanged;
}

bool StepEditor::editLimbs(SkeletonStep& step) const {
    bool isChanged = false;
    std::optional<usize> removed;
    const std::optional<std::string>& skeletonObject = step.object ? step.object : object;
    for (usize i = 0; i < step.limbs.size(); ++i) {
        LimbSteps& limb = step.limbs[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::format("Limb {}###limb", limb.limb);
        const bool isOpen = ImGui::TreeNode(label.c_str());
        ImGui::SameLine();
        if (drawIconButton(core::icons::Trash, "Remove what is drawn with this limb")) {
            removed = i;
        }
        if (isOpen) {
            int index = limb.limb;
            if (ImGui::InputInt("Limb, as PostLimbDraw counts", &index)) {
                limb.limb = static_cast<u8>(std::clamp(index, 1, 255));
                isChanged = true;
            }
            isChanged = drawStepsEditor(limb.steps, skeletonObject, context, true) || isChanged;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removed) {
        step.limbs.erase(step.limbs.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("Draw with a limb")) {
        const u8 next = step.limbs.empty() ? 1 : static_cast<u8>(step.limbs.back().limb + 1);
        step.limbs.push_back(LimbSteps{.limb = next});
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool drawAddStep(std::vector<DrawStep>& steps, const bool isInLimb) {
    if (!ImGui::BeginCombo("##add", "Add a step", ImGuiComboFlags_NoArrowButton)) {
        return false;
    }
    bool isAdded = false;
    for (usize kind = 0; kind < std::variant_size_v<decltype(DrawStep::step)>; ++kind) {
        const DrawStep step = makeStep(kind);
        const bool isAllowed = !isInLimb || (!std::holds_alternative<SkeletonStep>(step.step) &&
                                             !std::holds_alternative<ShadowStep>(step.step));
        if (isAllowed && ImGui::Selectable(getStepName(step))) {
            steps.push_back(step);
            isAdded = true;
        }
    }
    ImGui::EndCombo();
    return isAdded;
}

} // namespace

bool drawStepsEditor(std::vector<DrawStep>& steps, const std::optional<std::string>& object,
                     const EditContext& context, const bool isInLimb) {
    bool isChanged = false;
    std::optional<usize> removed;
    std::optional<std::pair<usize, usize>> swapped;
    const StepEditor editor(object, context);
    for (usize i = 0; i < steps.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::format("{}. {}###step", i + 1, getStepName(steps[i]));
        const bool isOpen = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine();
        if (i > 0 && drawIconButton(core::icons::ArrowUp, "Move up")) {
            swapped = std::pair(i - 1, i);
        }
        ImGui::SameLine();
        if (i + 1 < steps.size() && drawIconButton(core::icons::ArrowDown, "Move down")) {
            swapped = std::pair(i, i + 1);
        }
        ImGui::SameLine();
        if (drawIconButton(core::icons::Trash, "Remove this step")) {
            removed = i;
        }
        if (isOpen) {
            isChanged =
                std::visit([&editor](auto& step) { return editor.edit(step); }, steps[i].step) ||
                isChanged;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (swapped) {
        std::swap(steps[swapped->first], steps[swapped->second]);
        isChanged = true;
    }
    if (removed) {
        steps.erase(steps.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    return drawAddStep(steps, isInLimb) || isChanged;
}

} // namespace toki::actor
