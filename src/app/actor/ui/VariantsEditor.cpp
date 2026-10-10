#include "VariantsEditor.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/ConditionEditor.h"
#include "app/actor/ui/EditWidgets.h"
#include "app/actor/ui/PartsEditor.h"
#include "app/actor/ui/Sentence.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr f32 DefaultScale = 0.01F;

[[nodiscard]] std::string formatNumber(const std::optional<f32> value) {
    return value ? std::format("{:g}", *value) : std::string("the entry's");
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
    endProperties();
    return isChanged;
}

[[nodiscard]] bool drawLooks(ActorEntry& entry, ActorVariant& variant, const EditContext& context) {
    Sentence sentence;
    const auto edit = [&] { return editLooks(entry, variant, context); };
    sentence.addWords("Drawn from");
    bool isChanged = sentence.addToken(
        "##object", variant.object.value_or(entry.object.value_or("no object")), edit);
    sentence.addWords("at scale");
    isChanged = sentence.addToken(
                    "##scale", formatNumber(variant.scale ? variant.scale : entry.scale), edit) ||
                isChanged;
    if (variant.heightOffset || entry.heightOffset) {
        sentence.addWords("raised by");
        isChanged = sentence.addToken("##height",
                                      formatNumber(variant.heightOffset ? variant.heightOffset
                                                                        : entry.heightOffset),
                                      edit) ||
                    isChanged;
    }
    if (variant.draw) {
        sentence.addWords("as");
        const std::string_view label = getEntryLabel(DrawKindNames, *variant.draw);
        isChanged = sentence.addToken("##draw", std::string(label), edit) || isChanged;
    }
    return sentence.addToken("##more", "…", edit) || isChanged;
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

[[nodiscard]] bool drawChildren(std::vector<ChildActor>& children, const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    for (usize i = 0; i < children.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ChildActor& child = children[i];
        const auto edit = [&] { return editChild(child, context); };
        Sentence sentence;
        sentence.addWords("Spawns");
        isChanged =
            sentence.addToken("##actor", child.actor.empty() ? "(pick one)" : child.actor, edit) ||
            isChanged;
        sentence.addWords("with params");
        isChanged =
            sentence.addToken("##params", std::format("{:04X}", child.params), edit) || isChanged;
        sentence.addWords("at");
        isChanged = sentence.addToken("##offset",
                                      std::format("{:g}, {:g}, {:g}", child.offset[0],
                                                  child.offset[1], child.offset[2]),
                                      edit) ||
                    isChanged;
        if (sentence.addButton(core::icons::Trash, "Remove it")) {
            removed = i;
        }
        ImGui::PopID();
    }
    if (removed) {
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(*removed));
        isChanged = true;
    }
    if (ImGui::SmallButton("+ Child")) {
        children.emplace_back();
        isChanged = true;
    }
    return isChanged;
}

[[nodiscard]] bool drawDefaultParams(std::optional<u16>& params) {
    Sentence sentence;
    sentence.addWords("Placed new with params");
    return sentence.addToken("##default", params ? std::format("{:04X}", *params) : "0000", [&] {
        if (!beginProperties("default")) {
            return false;
        }
        u16 edited = params.value_or(0);
        const bool isChanged = editHex("Params", edited);
        if (isChanged) {
            params = edited;
        }
        endProperties();
        return isChanged;
    });
}

[[nodiscard]] bool drawVariant(ActorEntry& entry, ActorVariant& variant,
                               const EditContext& context) {
    ImGui::PushID("condition");
    bool isChanged = drawConditionEditor(variant.match, entry.params);
    ImGui::PopID();
    ImGui::PushID("looks");
    isChanged = drawLooks(entry, variant, context) || isChanged;
    ImGui::PopID();
    ImGui::Indent();
    ImGui::PushID("parts");
    isChanged =
        drawPartsEditor(variant.parts, variant.object ? variant.object : entry.object, context) ||
        isChanged;
    ImGui::PopID();
    ImGui::PushID("children");
    isChanged = drawChildren(variant.children, context) || isChanged;
    ImGui::PopID();
    ImGui::Unindent();
    ImGui::PushID("default");
    isChanged = drawDefaultParams(variant.defaultParams) || isChanged;
    ImGui::PopID();
    return isChanged;
}

} // namespace

bool drawVariantsEditor(ActorEntry& entry, const EditContext& context) {
    bool isChanged = false;
    std::optional<usize> removed;
    std::optional<usize> copied;
    std::optional<usize> raised;
    for (usize i = 0; i < entry.variants.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::format(
            "{}. {}###variant", i + 1, describeCondition(entry.variants[i].match, entry.params));
        const bool isOpen = ImGui::CollapsingHeader(
            label.c_str(), ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - (ImGui::GetFrameHeight() * 3.0F));
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
            isChanged = drawVariant(entry, entry.variants[i], context) || isChanged;
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
