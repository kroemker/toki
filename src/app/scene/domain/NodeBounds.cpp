#include "NodeBounds.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneEditor.h"

#include <glm/common.hpp>

#include <array>
#include <variant>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] glm::vec3 makePoint(const std::array<s16, 3>& position) {
    return {position[0], position[1], position[2]};
}

[[nodiscard]] std::vector<glm::vec3> listPoints(const Scene& scene, const SceneNode& node) {
    std::vector<glm::vec3> points;
    if (const auto* where = std::get_if<PlacementRef>(&node)) {
        if (const std::optional<Placement> placement = findPlacement(scene, *where)) {
            points.push_back(
                makePoint(std::visit([](const auto& held) { return held.position; }, *placement)));
        }
    } else if (const auto* path = std::get_if<PathRef>(&node)) {
        if (const std::optional<ScenePath> found = findPath(scene, path->layer, path->path)) {
            for (const std::array<s16, 3>& point : found->points) {
                points.push_back(makePoint(point));
            }
        }
    } else if (const auto* point = std::get_if<PathPointRef>(&node)) {
        const std::optional<ScenePath> found = findPath(scene, point->layer, point->path);
        if (found && point->point < found->points.size()) {
            points.push_back(makePoint(found->points[point->point]));
        }
    } else if (const auto* polygon = std::get_if<CollisionPolygonRef>(&node)) {
        const Collision* collision = findLayerCollision(scene, polygon->layer);
        if (collision != nullptr && polygon->polygon < collision->polygons.size()) {
            for (usize corner = 0; corner < 3; ++corner) {
                const u16 index = getVertexIndex(collision->polygons[polygon->polygon], corner);
                if (index < collision->vertices.size()) {
                    points.push_back(makePoint(collision->vertices[index]));
                }
            }
        }
    } else if (const auto* box = std::get_if<WaterBoxRef>(&node)) {
        const Collision* collision = findLayerCollision(scene, box->layer);
        if (collision != nullptr && box->box < collision->waterBoxes.size()) {
            const WaterBox& water = collision->waterBoxes[box->box];
            points.emplace_back(water.xMin, water.ySurface, water.zMin);
            points.emplace_back(water.xMin + water.xLength, water.ySurface,
                                water.zMin + water.zLength);
        }
    }
    return points;
}

} // namespace

std::optional<NodeBounds> findNodeBounds(const Scene& scene,
                                         const std::span<const SceneNode> nodes) {
    std::optional<NodeBounds> bounds;
    for (const SceneNode& node : nodes) {
        for (const glm::vec3& point : listPoints(scene, node)) {
            if (bounds) {
                bounds->min = glm::min(bounds->min, point);
                bounds->max = glm::max(bounds->max, point);
            } else {
                bounds = NodeBounds{.min = point, .max = point};
            }
        }
    }
    return bounds;
}

} // namespace toki::scene
