#include "PathForm.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/PathEdits.h"
#include "app/scene/domain/PathUsers.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

constexpr float LabelWidth = 4.5F;
/// How far a point added after a path's only point is put from it.
constexpr s16 NewPointStep = 100;

[[nodiscard]] std::array<s16, 3> makeInsertedPoint(const ScenePath& path, const usize after) {
    const std::array<s16, 3>& from = path.points[after];
    if (after + 1 < path.points.size()) {
        const std::array<s16, 3>& to = path.points[after + 1];
        return {static_cast<s16>((from[0] + to[0]) / 2), static_cast<s16>((from[1] + to[1]) / 2),
                static_cast<s16>((from[2] + to[2]) / 2)};
    }
    if (after == 0) {
        return {static_cast<s16>(from[0] + NewPointStep), from[1], from[2]};
    }
    const std::array<s16, 3>& before = path.points[after - 1];
    return {static_cast<s16>((2 * from[0]) - before[0]),
            static_cast<s16>((2 * from[1]) - before[1]),
            static_cast<s16>((2 * from[2]) - before[2])};
}

} // namespace

PathRef findPathOf(const SceneNode& where) {
    if (const auto* point = std::get_if<PathPointRef>(&where)) {
        return PathRef{.layer = point->layer, .path = point->path};
    }
    return std::get<PathRef>(where);
}

void PathForm::draw(const Scene& scene, const SceneNode& where, const usize selected) {
    const PathRef path = findPathOf(where);
    const std::optional<ScenePath> stored = findPath(scene, path.layer, path.path);
    if (!stored) {
        return;
    }
    if (!draft || draft->where != path) {
        draft = Draft{.where = path, .path = *stored};
    }
    const auto* point = std::get_if<PathPointRef>(&where);
    ImGui::TextColored(core::palette::Accent, "%s", core::icons::Path);
    ImGui::SameLine();
    ImGui::TextUnformatted(std::format("Path {}", path.path).c_str());
    ImGui::SetItemTooltip("Actors that follow a path name it by its index, in their params.");
    const auto followers = static_cast<usize>(
        std::ranges::count(listPathUsers(scene, path.layer), path.path, &PathUser::path));
    ImGui::TextColored(core::palette::TextMuted, "%s",
                       std::format("{} points · {} {} · {}", stored->points.size(), followers,
                                   followers == 1 ? "actor follows it" : "actors follow it",
                                   makeLayerName(scene.layers[path.layer].index))
                           .c_str());
    ImGui::SetItemTooltip("Counted by the params the actor database marks as paths.");
    if (selected > 1) {
        ImGui::TextColored(core::palette::Accent, "%zu selected; shown is the one picked last.",
                           selected);
    }
    ImGui::Spacing();
    drawPoints(scene, path, point != nullptr ? std::optional(point->point) : std::nullopt);
    if (draft && draft->path != *stored && !ImGui::IsAnyItemActive()) {
        pickPoints(scene, path, draft->path, std::nullopt);
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Danger);
    if (ImGui::Button("Delete Path")) {
        askRemove(scene, path);
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("The paths after it move down one, and the actors that follow them are "
                          "renumbered. Actors of a room header another layer reads too are left "
                          "as they are.");
}

void PathForm::drawPoints(const Scene& scene, const PathRef& path,
                          const std::optional<usize> selectedPoint) {
    if (!ImGui::CollapsingHeader("Points", ImGuiTreeNodeFlags_DefaultOpen) ||
        !core::beginProperties("##points", LabelWidth)) {
        return;
    }
    const float buttons = (ImGui::GetFrameHeight() * 2.0F) + ImGui::GetStyle().ItemSpacing.x * 2.0F;
    std::vector<std::array<s16, 3>>& points = draft->path.points;
    for (usize i = 0; i < points.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool isSelected = i == selectedPoint;
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Accent);
        }
        core::drawPropertyLabel(std::format("Point {}", i), "Where the point is, in world units.");
        if (isSelected) {
            ImGui::PopStyleColor();
        }
        ImGui::SetNextItemWidth(-buttons);
        ImGui::DragScalarN("##point", ImGuiDataType_S16, points[i].data(), 3, 1.0F);
        ImGui::SameLine();
        ImGui::BeginDisabled(points.size() >= MaximumPathPoints);
        if (ImGui::SmallButton(core::icons::Plus)) {
            pickInsert(scene, path, i);
        }
        ImGui::SetItemTooltip("Add a point after it.");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(points.size() <= 1);
        if (ImGui::SmallButton(core::icons::X)) {
            pickRemovePoint(scene, {.layer = path.layer, .path = path.path, .point = i});
        }
        ImGui::SetItemTooltip("%s", points.size() <= 1 ? "A path keeps at least one point."
                                                       : "Remove the point.");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    core::endProperties();
}

void PathForm::drawMenu(const Scene& scene, const SceneNode& where) {
    const PathRef path = findPathOf(where);
    const std::optional<ScenePath> stored = findPath(scene, path.layer, path.path);
    if (!stored) {
        return;
    }
    if (const auto* point = std::get_if<PathPointRef>(&where)) {
        if (ImGui::MenuItem("Insert Point After", nullptr, false,
                            stored->points.size() < MaximumPathPoints)) {
            pickInsert(scene, path, point->point);
        }
        if (ImGui::MenuItem("Delete Point", nullptr, false, stored->points.size() > 1)) {
            pickRemovePoint(scene, *point);
        }
        ImGui::Separator();
    } else if (ImGui::MenuItem("Add Point", nullptr, false,
                               stored->points.size() < MaximumPathPoints)) {
        pickInsert(scene, path, stored->points.size() - 1);
    }
    if (ImGui::MenuItem("Delete Path")) {
        askRemove(scene, path);
    }
}

void PathForm::drawGroupMenu(const Scene& scene, const usize layer,
                             const std::array<s16, 3>& position) {
    if (layer >= scene.layers.size() || !ImGui::MenuItem("Add Path")) {
        return;
    }
    const usize gameLayer = scene.layers[layer].index;
    pick(
        [gameLayer, position](SceneModel& model) {
            return addPath(model, gameLayer, std::span(&position, 1)).transform([](usize) {});
        },
        PathPointRef{.layer = layer, .path = scene.layers[layer].header.paths.size()});
}

std::optional<PathForm::Draft> PathForm::findChangedDraft(const Scene& scene) const {
    if (!draft ||
        findPath(scene, draft->where.layer, draft->where.path) == std::optional(draft->path)) {
        return std::nullopt;
    }
    return draft;
}

void PathForm::pickPoints(const Scene& scene, const PathRef& path, ScenePath points,
                          std::optional<SceneNode> select) {
    const usize gameLayer = scene.layers[path.layer].index;
    pick([gameLayer, index = path.path, points = std::move(points)](
             SceneModel& model) { return setPathPoints(model, gameLayer, index, points.points); },
         std::move(select));
}

void PathForm::pickInsert(const Scene& scene, const PathRef& path, const usize after) {
    std::optional<ScenePath> points = findPath(scene, path.layer, path.path);
    if (!points || after >= points->points.size()) {
        return;
    }
    const std::array<s16, 3> inserted = makeInsertedPoint(*points, after);
    points->points.insert(points->points.begin() + static_cast<std::ptrdiff_t>(after + 1),
                          inserted);
    pickPoints(scene, path, std::move(*points),
               PathPointRef{.layer = path.layer, .path = path.path, .point = after + 1});
}

void PathForm::pickRemovePoint(const Scene& scene, const PathPointRef& point) {
    std::optional<ScenePath> points = findPath(scene, point.layer, point.path);
    if (!points || point.point >= points->points.size()) {
        return;
    }
    points->points.erase(points->points.begin() + static_cast<std::ptrdiff_t>(point.point));
    pickPoints(scene, {.layer = point.layer, .path = point.path}, std::move(*points),
               PathRef{.layer = point.layer, .path = point.path});
}

void PathForm::askRemove(const Scene& scene, const PathRef& path) {
    if (std::ranges::contains(listPathUsers(scene, path.layer), path.path, &PathUser::path)) {
        askedRemoval = path;
        return;
    }
    pickRemove(scene, path);
}

void PathForm::drawRemovalQuestion(const Scene& scene) {
    constexpr const char* Title = "Delete Path###pathRemoval";
    if (askedRemoval && !ImGui::IsPopupOpen(Title)) {
        ImGui::OpenPopup(Title);
    }
    if (!ImGui::BeginPopupModal(Title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (!askedRemoval || !findPath(scene, askedRemoval->layer, askedRemoval->path)) {
        askedRemoval.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const PathRef path = *askedRemoval;
    ImGui::TextUnformatted(std::format("Path {} is followed by:", path.path).c_str());
    for (const PathUser& user : listPathUsers(scene, path.layer)) {
        if (user.path != path.path) {
            continue;
        }
        const auto model = scene.actors.models.find(user.actorId);
        const std::string name = model == scene.actors.models.end()
                                     ? std::format("actor 0x{:04X}", static_cast<u16>(user.actorId))
                                     : (model->second.entry && !model->second.entry->name.empty()
                                            ? model->second.entry->name
                                            : model->second.enumName);
        const std::string where = user.where.kind == PlacementKind::Actor
                                      ? std::format("room {}", user.where.room)
                                      : std::string("between rooms");
        ImGui::BulletText("%s", std::format("{}, {}, by \"{}\"", name, where, user.param).c_str());
    }
    ImGui::TextColored(core::palette::TextMuted, "%s",
                       "Their params are left as they are: they follow the path that moves into "
                       "its place, or none when it is the last.");
    ImGui::TextColored(core::palette::TextMuted, "%s",
                       "Not every actor reads a value as no path, so they are not set to one.");
    ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Danger);
    if (ImGui::Button("Delete Path")) {
        askedRemoval.reset();
        ImGui::CloseCurrentPopup();
        pickRemove(scene, path);
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Keep It")) {
        askedRemoval.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void PathForm::pickRemove(const Scene& scene, const PathRef& path) {
    const usize gameLayer = scene.layers[path.layer].index;
    pick(
        [gameLayer, index = path.path,
         changes = listPathRemovalChanges(scene, path.layer, path.path)](
            SceneModel& model) -> std::expected<void, base::Diagnostic> {
            if (std::expected<void, base::Diagnostic> removed = removePath(model, gameLayer, index);
                !removed) {
                return removed;
            }
            return setPlacements(model, changes);
        },
        std::nullopt);
}

} // namespace toki::scene
