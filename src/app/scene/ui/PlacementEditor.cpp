#include "PlacementEditor.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <imgui.h>

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
/// What a new actor is until it is picked: a green rupee, at params 0, which
/// any room can hold.
constexpr std::string_view DefaultActor = "ACTOR_EN_ITEM00";

[[nodiscard]] actor::PlacementValues makeValues(const Scene& scene, const PlacementRef& where,
                                                const Placement& placement) {
    const std::optional<actor::Age> age = findLayerAge(scene.layers[where.layer].index);
    if (const auto* transition = std::get_if<TransitionActorPlacement>(&placement)) {
        return makePlacementValues(scene.actors, static_cast<u16>(transition->params),
                                   {0, transition->rotationY, 0}, age);
    }
    const auto& actor = std::get<ActorPlacement>(placement);
    return makePlacementValues(scene.actors, static_cast<u16>(actor.params), actor.rotation, age);
}

void setValues(Placement& placement, const actor::PlacementValues& values) {
    if (auto* transition = std::get_if<TransitionActorPlacement>(&placement)) {
        transition->params = static_cast<s16>(values.params);
        transition->rotationY = values.rotation[1];
        return;
    }
    auto& actor = std::get<ActorPlacement>(placement);
    actor.params = static_cast<s16>(values.params);
    actor.rotation = values.rotation;
}

[[nodiscard]] std::array<bool, 3> findParamAxes(const actor::ActorEntry* entry) {
    std::array<bool, 3> axes{};
    if (entry == nullptr) {
        return axes;
    }
    for (const actor::ActorParam& param : entry->params) {
        if (param.source != actor::ParamSource::Params) {
            axes[static_cast<usize>(param.source) - 1] = true;
        }
    }
    return axes;
}

[[nodiscard]] std::string makeWhere(const PlacementRef& where) {
    switch (where.kind) {
    case PlacementKind::Actor:
        return std::format("Room {} · #{}", where.room, where.index);
    case PlacementKind::TransitionActor:
        return std::format("Transition actor #{}", where.index);
    case PlacementKind::PlayerEntry:
        return std::format("Player entry #{}", where.index);
    }
    return {};
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

PlacementEditor::PlacementEditor(const workspace::WorkspaceService& workspaceService,
                                 actor::CollectionCatalog& collectionCatalog, Show show)
    : workspaceService(workspaceService), collectionCatalog(collectionCatalog),
      show(std::move(show)), actorForm(workspaceService) {}

void PlacementEditor::refresh(const Scene& scene) {
    draft.reset();
    if (selected && !findPlacement(scene, *selected)) {
        selected.reset();
    }
}

bool PlacementEditor::apply(Scene& scene, const PlacementRef& where, const SceneEditor::Edit& edit,
                            const bool joinsLast) {
    const std::expected<void, base::Diagnostic> applied =
        sceneEditor.apply(scene, where, edit, joinsLast);
    draft.reset();
    if (!applied) {
        failure = applied.error().message;
        return false;
    }
    failure.clear();
    showChanged(scene);
    return true;
}

void PlacementEditor::showChanged(Scene& scene) {
    refresh(scene);
    show(hasUnreadActors(scene));
}

void PlacementEditor::undo(Scene& scene) {
    if (sceneEditor.undo(scene)) {
        failure.clear();
        showChanged(scene);
    }
}

void PlacementEditor::redo(Scene& scene) {
    if (sceneEditor.redo(scene)) {
        failure.clear();
        showChanged(scene);
    }
}

void PlacementEditor::addObjectFor(Scene& scene, const PlacementRef& where, const s16 actorId) {
    if (findActorObject) {
        if (const std::optional<std::string> object = findActorObject(actorId)) {
            addObject(scene, where, *object, true);
        }
    }
}

void PlacementEditor::addObject(Scene& scene, const PlacementRef& where, const std::string& object,
                                const bool joinsLast) {
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (where.kind != PlacementKind::Actor || workspace == nullptr) {
        return;
    }
    const std::optional<s16> missing =
        findMissingObject(scene, where, object, workspace->getTables());
    if (!missing) {
        return;
    }
    const bool isAdded = apply(
        scene, where,
        [missing](SceneModel& model, const PlacementList& list) {
            return addRoomObjects(model, list.layer, list.room, std::span(&*missing, 1));
        },
        joinsLast);
    if (!isAdded) {
        failure = std::format("{} is not in the room's object list, and was not added: {}", object,
                              failure);
    }
}

void PlacementEditor::drawInspector(Scene& scene) {
    if (!selected) {
        ImGui::TextColored(core::palette::TextMuted,
                           "Select a placement in the Scene panel or the viewport.");
        return;
    }
    if (!draft) {
        draft = findPlacement(scene, *selected);
        if (!draft) {
            selected.reset();
            return;
        }
    }
    const PlacementRef where = *selected;
    const s16 actorId = std::visit([](const auto& held) { return held.id; }, *draft);
    const auto model = scene.actors.models.find(actorId);
    const actor::ActorEntry* entry =
        model != scene.actors.models.end() && model->second.entry ? &*model->second.entry : nullptr;
    drawHeader(where, actorId, entry);
    bool isDone = false;
    if (ImGui::CollapsingHeader("Actor", ImGuiTreeNodeFlags_DefaultOpen) &&
        core::beginProperties("##actor", LabelWidth)) {
        const std::array<bool, 3> axes = findParamAxes(entry);
        if (auto* actor = std::get_if<ActorPlacement>(&*draft)) {
            isDone = actorForm.drawActor(*actor, axes);
        } else if (auto* transition = std::get_if<TransitionActorPlacement>(&*draft)) {
            isDone = actorForm.drawTransitionActor(*transition, axes[1]);
        }
        core::endProperties();
    }
    isDone = drawParams(scene, where, entry, actorId) || isDone;
    if (entry != nullptr) {
        drawObject(scene, where, *entry);
    }
    drawActions(where);
    if (isDone) {
        commitDraft(scene, where);
    }
}

void PlacementEditor::drawHeader(const PlacementRef& where, const s16 actorId,
                                 const actor::ActorEntry* entry) const {
    std::string enumName = std::format("0x{:04X}", static_cast<u16>(actorId));
    if (const hackeroot::Workspace* workspace = workspaceService.getWorkspace()) {
        const std::vector<hackeroot::ActorEntry>& actors = workspace->getTables().actors;
        const auto index = static_cast<usize>(static_cast<u16>(actorId));
        if (index < actors.size() && !actors[index].enumName.empty()) {
            enumName = actors[index].enumName;
        }
    }
    const std::string name = entry != nullptr && !entry->name.empty() ? entry->name : enumName;
    ImGui::TextColored(core::palette::Accent, "%s", core::icons::Cube);
    ImGui::SameLine();
    ImGui::TextUnformatted(name.c_str());
    const std::string details =
        name == enumName ? makeWhere(where) : std::format("{} · {}", enumName, makeWhere(where));
    ImGui::TextColored(core::palette::TextMuted, "%s", details.c_str());
    ImGui::Spacing();
}

bool PlacementEditor::drawParams(const Scene& scene, const PlacementRef& where,
                                 const actor::ActorEntry* entry, const s16 actorId) {
    if (!ImGui::CollapsingHeader("Params", ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }
    if (entry == nullptr || entry->params.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "%s",
                           entry == nullptr ? "The actor database has no entry for this actor."
                                            : "Its entry in the actor database names no params.");
        ImGui::TextColored(core::palette::TextMuted, "The Actor section's Params field sets them.");
        if (const auto model = scene.actors.models.find(actorId);
            openActorEditor && model != scene.actors.models.end() &&
            ImGui::Button("Edit in the Actor Database")) {
            openActorEditor(model->second.enumName);
        }
        return false;
    }
    if (!core::beginProperties("##params", LabelWidth)) {
        return false;
    }
    actor::PlacementValues values = makeValues(scene, where, *draft);
    const bool isDone =
        paramsForm.draw(*entry, values, [this, &entry](const std::string_view name) {
            return collectionCatalog.findCollection(entry->actor, name);
        });
    setValues(*draft, values);
    if (!entry->variants.empty()) {
        core::drawPropertyLabel("Variant", "The first variant of the actor's entry whose match "
                                           "these params hold for, which is what is drawn.");
        const actor::ActorVariant* variant = actor::findVariant(*entry, values);
        const std::string text =
            variant != nullptr ? std::format("{} of {}", variant - entry->variants.data() + 1,
                                             entry->variants.size())
                               : std::format("None of {} matches: marked", entry->variants.size());
        ImGui::TextColored(variant != nullptr ? core::palette::Text : core::palette::Warning, "%s",
                           text.c_str());
    }
    core::endProperties();
    if (openActorEditor && ImGui::SmallButton("Edit in the Actor Database")) {
        openActorEditor(entry->actor);
    }
    return isDone;
}

void PlacementEditor::drawObject(const Scene& scene, const PlacementRef& where,
                                 const actor::ActorEntry& entry) {
    const actor::ActorVariant* variant =
        actor::findVariant(entry, makeValues(scene, where, *draft));
    const std::optional<std::string> object =
        variant != nullptr && variant->object ? variant->object : entry.object;
    if (!object || !ImGui::CollapsingHeader("Object", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##object", LabelWidth)) {
        return;
    }
    core::drawPropertyLabel("Needs", "The object the actor database says the actor is drawn "
                                     "from, which the game must have loaded to spawn it.");
    ImGui::TextUnformatted(object->c_str());
    const hackeroot::Workspace* workspace = workspaceService.getWorkspace();
    if (where.kind == PlacementKind::Actor && workspace != nullptr) {
        core::drawPropertyLabel("Loaded", "Whether a keep object or the room's object list in "
                                          "this layer loads it.");
        if (!findMissingObject(scene, where, *object, workspace->getTables())) {
            ImGui::TextColored(core::palette::Success, "%s Yes", core::icons::Check);
        } else {
            ImGui::TextColored(core::palette::Warning, "%s No", core::icons::Warning);
            ImGui::SameLine();
            if (ImGui::SmallButton("Add to the Object List")) {
                picked = [this, where, name = *object](Scene& edited) {
                    addObject(edited, where, name);
                };
            }
        }
    }
    core::endProperties();
}

void PlacementEditor::drawActions(const PlacementRef& where) {
    ImGui::Spacing();
    ImGui::BeginDisabled(where.kind == PlacementKind::PlayerEntry);
    if (ImGui::Button("Duplicate")) {
        pickDuplicate(where, *draft);
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Danger);
    if (ImGui::Button("Delete")) {
        pickDelete(where);
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (where.kind == PlacementKind::PlayerEntry) {
        ImGui::SetItemTooltip("The spawn list names the player entries by index.");
    }
}

void PlacementEditor::commitDraft(Scene& scene, const PlacementRef& where) {
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

void PlacementEditor::pickDuplicate(const PlacementRef& where, const Placement& placement) {
    picked = [this, where, copied = placement](Scene& edited) { add(edited, where, copied); };
}

void PlacementEditor::pickDelete(const PlacementRef& where) {
    picked = [this, where](Scene& edited) {
        if (selected == where) {
            selected.reset();
        }
        apply(edited, where, [where](SceneModel& model, const PlacementList& list) {
            return removePlacement(model, list, where.index);
        });
    };
}

void PlacementEditor::drawPlacementMenu(const Scene& scene, const PlacementRef& where) {
    const std::optional<Placement> placement = findPlacement(scene, where);
    if (!placement) {
        return;
    }
    ImGui::Separator();
    const bool isPlayerEntry = where.kind == PlacementKind::PlayerEntry;
    if (ImGui::MenuItem("Duplicate", nullptr, false, !isPlayerEntry)) {
        pickDuplicate(where, *placement);
    }
    if (ImGui::MenuItem("Delete", nullptr, false, !isPlayerEntry)) {
        pickDelete(where);
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

std::optional<std::array<s16, 3>> PlacementEditor::findSelectedPosition(const Scene& scene) const {
    if (!selected) {
        return std::nullopt;
    }
    const std::optional<Placement> placement = draft ? draft : findPlacement(scene, *selected);
    if (!placement) {
        return std::nullopt;
    }
    return std::visit([](const auto& held) { return held.position; }, *placement);
}

void PlacementEditor::moveSelected(const Scene& scene, const std::array<s16, 3>& position,
                                   const bool isDone) {
    if (!selected) {
        return;
    }
    if (!draft) {
        draft = findPlacement(scene, *selected);
    }
    if (!draft) {
        return;
    }
    std::visit([&position](auto& held) { held.position = position; }, *draft);
    if (!isDone) {
        return;
    }
    picked = [this, where = *selected, moved = *draft](Scene& edited) {
        apply(edited, where, [&moved, &where](SceneModel& model, const PlacementList& list) {
            if (const auto* transition = std::get_if<TransitionActorPlacement>(&moved)) {
                return setTransitionActor(model, list.layer, where.index, *transition);
            }
            return setPlacement(model, list, where.index, std::get<ActorPlacement>(moved));
        });
    };
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
