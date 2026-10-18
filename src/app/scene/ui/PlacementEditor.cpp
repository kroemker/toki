#include "PlacementEditor.h"

#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <array>
#include <cfloat>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace toki::scene {

namespace {

constexpr float LabelWidth = 7.0F;
constexpr float DragSpeed = 1.0F;
/// What a new actor is until it is picked: a green rupee, at params 0, which
/// any room can hold.
constexpr std::string_view DefaultActor = "ACTOR_EN_ITEM00";

[[nodiscard]] std::string makeActorLabel(const hackeroot::ActorEntry& actor) {
    return actor.enumName.empty() ? std::format("0x{:04X}", actor.id) : actor.enumName;
}

[[nodiscard]] bool isFilteredIn(const std::string& label, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
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

} // namespace

s16 PlacementEditor::findDefaultActor() const {
    if (const hackeroot::Workspace* workspace = workspaceService.getWorkspace()) {
        for (const hackeroot::ActorEntry& actor : workspace->getTables().actors) {
            if (actor.enumName == DefaultActor) {
                return static_cast<s16>(actor.id);
            }
        }
    }
    return 1;
}

PlacementEditor::PlacementEditor(const workspace::WorkspaceService& workspaceService, Show show)
    : workspaceService(workspaceService), show(std::move(show)) {}

void PlacementEditor::refresh(const Scene& scene) {
    draft.reset();
    if (selected && !findPlacement(scene, *selected)) {
        selected.reset();
    }
}

bool PlacementEditor::apply(Scene& scene, const PlacementRef& where,
                            const SceneEditor::Edit& edit) {
    const std::expected<void, base::Diagnostic> applied = sceneEditor.apply(scene, where, edit);
    draft.reset();
    if (!applied) {
        failure = applied.error().message;
        return false;
    }
    failure.clear();
    refresh(scene);
    show(hasUnreadActors(scene));
    return true;
}

void PlacementEditor::addObjectFor(Scene& scene, const PlacementRef& where, const s16 actorId) {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (where.kind != PlacementKind::Actor || workspace == nullptr || !findActorObject) {
        return;
    }
    const std::optional<std::string> object = findActorObject(actorId);
    const std::optional<s16> missing =
        object ? findMissingObject(scene, where, *object, workspace->getTables()) : std::nullopt;
    if (!missing) {
        return;
    }
    const bool isAdded =
        apply(scene, where, [missing](SceneModel& model, const PlacementList& list) {
            return addRoomObjects(model, list.layer, list.room, std::span(&*missing, 1));
        });
    if (!isAdded) {
        failure = std::format("{} is not in the room's object list, and was not added: {}", *object,
                              failure);
    }
}

bool PlacementEditor::editActorId(s16& id) const {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    core::drawPropertyLabel("Actor", "The actor's ID, from the actor table.");
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

bool PlacementEditor::editActor(ActorPlacement& placement) const {
    bool isDone = editActorId(placement.id);
    isDone = editPosition(placement.position) || isDone;
    core::drawPropertyLabel("Rotation",
                            "In binary angles, 0x4000 a quarter turn. Many actors read parts of "
                            "the X and Z rotation as params instead.");
    const float third =
        (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0F;
    for (usize axis = 0; axis < 3; ++axis) {
        ImGui::PushID(static_cast<int>(axis));
        if (axis > 0) {
            ImGui::SameLine();
        }
        auto word = static_cast<u16>(placement.rotation[axis]);
        ImGui::SetNextItemWidth(third);
        ImGui::InputScalar("##rotation", ImGuiDataType_U16, &word, nullptr, nullptr, "%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        placement.rotation[axis] = static_cast<s16>(word);
        isDone = ImGui::IsItemDeactivatedAfterEdit() || isDone;
        ImGui::PopID();
    }
    core::drawPropertyLabel("Params", "The actor's params, which its Init reads.");
    isDone = editHex("##params", placement.params) || isDone;
    return isDone;
}

bool PlacementEditor::editTransitionActor(TransitionActorPlacement& placement) const {
    bool isDone = editActorId(placement.id);
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
    core::drawPropertyLabel("Rotation Y", "In binary angles, 0x4000 a quarter turn.");
    isDone = editHex("##rotationY", placement.rotationY) || isDone;
    core::drawPropertyLabel("Params", "The actor's params, which its Init reads.");
    isDone = editHex("##params", placement.params) || isDone;
    return isDone;
}

void PlacementEditor::drawInspector(Scene& scene) {
    if (!selected) {
        return;
    }
    if (!draft) {
        draft = findPlacement(scene, *selected);
        if (!draft) {
            selected.reset();
            return;
        }
    }
    if (!ImGui::CollapsingHeader("Selected", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (!core::beginProperties("##placement", LabelWidth)) {
        return;
    }
    const PlacementRef where = *selected;
    bool isDone = false;
    if (auto* actor = std::get_if<ActorPlacement>(&*draft)) {
        isDone = editActor(*actor);
    } else if (auto* transition = std::get_if<TransitionActorPlacement>(&*draft)) {
        isDone = editTransitionActor(*transition);
    }
    core::endProperties();
    if (!isDone) {
        return;
    }
    const Placement edited = *draft;
    const bool isSet =
        apply(scene, where, [&edited, &where](SceneModel& model, const PlacementList& list) {
            if (const auto* transition = std::get_if<TransitionActorPlacement>(&edited)) {
                return setTransitionActor(model, list.layer, where.index, *transition);
            }
            return setPlacement(model, list, where.index, std::get<ActorPlacement>(edited));
        });
    if (const auto* actor = std::get_if<ActorPlacement>(&edited); isSet && actor != nullptr) {
        addObjectFor(scene, where, actor->id);
    }
}

void PlacementEditor::drawPlacementMenu(const Scene& scene, const PlacementRef& where) {
    const std::optional<Placement> placement = findPlacement(scene, where);
    if (!placement) {
        return;
    }
    ImGui::Separator();
    const bool isPlayerEntry = where.kind == PlacementKind::PlayerEntry;
    if (ImGui::MenuItem("Duplicate", nullptr, false, !isPlayerEntry)) {
        picked = [this, where, copied = *placement](Scene& edited) { add(edited, where, copied); };
    }
    if (ImGui::MenuItem("Delete", nullptr, false, !isPlayerEntry)) {
        picked = [this, where](Scene& edited) {
            if (selected == where) {
                selected.reset();
            }
            apply(edited, where, [where](SceneModel& model, const PlacementList& list) {
                return removePlacement(model, list, where.index);
            });
        };
    }
    if (isPlayerEntry) {
        ImGui::SetItemTooltip("The spawn list names the player entries by index.");
    }
}

void PlacementEditor::drawRoomMenu(const usize layer, const usize room,
                                   const std::array<s16, 3>& position) {
    ImGui::Separator();
    if (ImGui::MenuItem("Add Actor")) {
        picked = [this, layer, room,
                  placement = ActorPlacement{.id = findDefaultActor(), .position = position}](
                     Scene& edited) {
            add(edited, PlacementRef{.kind = PlacementKind::Actor, .layer = layer, .room = room},
                placement);
        };
    }
}

void PlacementEditor::applyPicked(Scene& scene) {
    if (picked) {
        std::exchange(picked, nullptr)(scene);
    }
}

void PlacementEditor::add(Scene& scene, const PlacementRef& where, const Placement& placement) {
    std::optional<usize> added;
    apply(scene, where, [&placement, &added](SceneModel& model, const PlacementList& list) {
        std::expected<usize, base::Diagnostic> index =
            list.kind == PlacementKind::TransitionActor
                ? addTransitionActor(model, list.layer,
                                     std::get<TransitionActorPlacement>(placement))
                : addPlacement(model, list, std::get<ActorPlacement>(placement));
        if (!index) {
            return std::expected<void, base::Diagnostic>(std::unexpected(index.error()));
        }
        added = *index;
        return std::expected<void, base::Diagnostic>();
    });
    if (added) {
        PlacementRef chosen = where;
        chosen.index = *added;
        select(chosen);
        if (const auto* actor = std::get_if<ActorPlacement>(&placement)) {
            addObjectFor(scene, chosen, actor->id);
        }
    }
}

} // namespace toki::scene
