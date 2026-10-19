#include "ActorForm.h"

#include "app/core/ui/PropertyTable.h"
#include "app/scene/domain/BinaryAngle.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <array>
#include <cfloat>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

constexpr float DragSpeed = 1.0F;
/// A binary angle dragged turns as fast as one in degrees, a degree a point.
constexpr float BinaryDragSpeed = 0x10000 / 360.0F;
/// The width of the button that switches how rotations are shown, in frame
/// heights.
constexpr float AngleButtonWidth = 1.4F;

[[nodiscard]] std::string makeActorLabel(const hackeroot::ActorEntry& actor) {
    return actor.enumName.empty() ? std::format("0x{:04X}", actor.id) : actor.enumName;
}

[[nodiscard]] bool isFilteredIn(const std::string& label, const std::string& filter) {
    const auto lower = [](const char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    };
    std::string text;
    std::string wanted;
    for (const char c : label) {
        text += lower(c);
    }
    for (const char c : filter) {
        wanted += lower(c);
    }
    return text.contains(wanted);
}

[[nodiscard]] bool editHex(const char* id, s16& value) {
    auto word = static_cast<u16>(value);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputScalar(id, ImGuiDataType_U16, &word, nullptr, nullptr, "%04X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    value = static_cast<s16>(word);
    return ImGui::IsItemDeactivatedAfterEdit();
}

[[nodiscard]] bool editPosition(std::array<s16, 3>& position) {
    core::drawPropertyLabel("Position", "In world units, as the game places the actor.");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragScalarN("##position", ImGuiDataType_S16, position.data(), 3, DragSpeed);
    return ImGui::IsItemDeactivatedAfterEdit();
}

[[nodiscard]] bool editParams(s16& params) {
    core::drawPropertyLabel("Params",
                            "The actor's params as stored, which its Init reads. The Params "
                            "section below shows them as the actor database names their bits.");
    return editHex("##params", params);
}

} // namespace

bool ActorForm::editType(s16& id) {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    core::drawPropertyLabel("Type", "The actor's ID, from the actor table.");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (workspace == nullptr) {
        return editHex("##actor", id);
    }
    const std::vector<hackeroot::ActorEntry>& actors = workspace->getTables().actors;
    const auto index = static_cast<usize>(static_cast<u16>(id));
    const std::string current =
        index < actors.size() ? makeActorLabel(actors[index]) : std::format("0x{:04X}", index);
    if (!ImGui::BeginCombo("##actor", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        return false;
    }
    if (ImGui::IsWindowAppearing()) {
        actorFilter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter", &actorFilter);
    bool isChanged = false;
    for (const hackeroot::ActorEntry& actor : actors) {
        const std::string label = makeActorLabel(actor);
        if (actor.isUnset || !isFilteredIn(label, actorFilter)) {
            continue;
        }
        if (ImGui::Selectable(label.c_str(), actor.id == index)) {
            id = static_cast<s16>(actor.id);
            isChanged = true;
        }
    }
    ImGui::EndCombo();
    return isChanged;
}

bool ActorForm::editRotation(const std::span<s16> angles, const std::span<const bool> isParamAxis) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float button = ImGui::GetFrameHeight() * AngleButtonWidth;
    const auto count = static_cast<float>(angles.size());
    const float width =
        (ImGui::GetContentRegionAvail().x - button - count * style.ItemSpacing.x) / count;
    bool isDone = false;
    for (usize axis = 0; axis < angles.size(); ++axis) {
        ImGui::PushID(static_cast<int>(axis));
        ImGui::SetNextItemWidth(width);
        if (isParamAxis[axis]) {
            auto word = static_cast<u16>(angles[axis]);
            ImGui::InputScalar("##rotation", ImGuiDataType_U16, &word, nullptr, nullptr, "%04X",
                               ImGuiInputTextFlags_CharsHexadecimal);
            angles[axis] = static_cast<s16>(word);
            ImGui::SetItemTooltip("The actor reads params from this axis, so it is shown "
                                  "raw. The Params section names its bits.");
        } else if (angleFormat == core::AngleFormat::Degrees) {
            f32 degrees = convertToDegrees(angles[axis]);
            if (ImGui::DragFloat("##rotation", &degrees, DragSpeed, 0.0F, 0.0F, "%.2f°")) {
                angles[axis] = convertToBinaryAngle(degrees);
            }
        } else {
            int word = static_cast<u16>(angles[axis]);
            if (ImGui::DragInt("##rotation", &word, BinaryDragSpeed, 0, 0, "%04X")) {
                angles[axis] = static_cast<s16>(static_cast<u16>(word & 0xFFFF));
            }
        }
        isDone = ImGui::IsItemDeactivatedAfterEdit() || isDone;
        ImGui::PopID();
        ImGui::SameLine();
    }
    drawAngleFormatButton();
    return isDone;
}

void ActorForm::drawAngleFormatButton() {
    const bool isDegrees = angleFormat == core::AngleFormat::Degrees;
    if (ImGui::Button(isDegrees ? "°##angles" : "0x##angles",
                      ImVec2(ImGui::GetFrameHeight() * AngleButtonWidth, 0.0F))) {
        angleFormat = isDegrees ? core::AngleFormat::Binary : core::AngleFormat::Degrees;
        if (changeAngleFormat) {
            changeAngleFormat(angleFormat);
        }
    }
    ImGui::SetItemTooltip(isDegrees ? "In degrees. Click for binary angles."
                                    : "In binary angles. Click for degrees.");
}

bool ActorForm::drawActor(ActorPlacement& placement, const std::array<bool, 3>& isParamAxis) {
    bool isDone = editType(placement.id);
    isDone = editPosition(placement.position) || isDone;
    core::drawPropertyLabel("Rotation",
                            "The actor's rotation about X, Y and Z, which the game applies in the "
                            "order Y, X, Z. Stored as binary angles, 0x4000 a quarter turn. Many "
                            "actors read parts of the X and Z rotation as params instead. Drag to "
                            "turn, or double-click to type.");
    isDone = editRotation(placement.rotation, isParamAxis) || isDone;
    isDone = editParams(placement.params) || isDone;
    return isDone;
}

bool ActorForm::drawTransitionActor(TransitionActorPlacement& placement,
                                    const bool isRotationParams) {
    bool isDone = editType(placement.id);
    for (usize side = 0; side < 2; ++side) {
        core::drawPropertyLabel(side == 0 ? "Front Room" : "Back Room",
                                "The room on this side, or -1 for none.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushID(static_cast<int>(side));
        ImGui::InputScalar("##room", ImGuiDataType_S8, &placement.rooms[side]);
        isDone = ImGui::IsItemDeactivatedAfterEdit() || isDone;
        ImGui::PopID();
    }
    isDone = editPosition(placement.position) || isDone;
    core::drawPropertyLabel("Rotation Y", "Stored as a binary angle, 0x4000 a quarter turn. "
                                          "Drag to turn, or double-click to type.");
    const std::array<bool, 1> isParamAxis{isRotationParams};
    isDone = editRotation(std::span(&placement.rotationY, 1), isParamAxis) || isDone;
    isDone = editParams(placement.params) || isDone;
    return isDone;
}

} // namespace toki::scene
