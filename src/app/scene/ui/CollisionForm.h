#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionNames.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneSelection.h"
#include "app/scene/domain/SurfaceRules.h"
#include "app/scene/ui/CollisionRows.h"

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

struct Scene;
struct SceneLayer;

/// The Properties panel for collision polygons selected: what the game reads
/// of the one picked last, and its surface type, each setting edited in place.
/// What passes through a polygon and which surface type it has are set for
/// every polygon selected; a surface type's settings change every polygon
/// that has it.
class CollisionForm {
public:
    /// Makes `edit` as one step once the panels are drawn, and selects
    /// `select` when it was made.
    using Pick = std::function<void(SceneEditor::ModelEdit edit, std::optional<SceneNode> select)>;

    explicit CollisionForm(Pick pick) : pick(std::move(pick)) {}

    /// Names the values of surface types' settings as `names` does.
    void setNames(const CollisionNames* names) {
        collisionNames = names;
    }

    /// `polygons` are those selected of one layer's collision, the one shown
    /// last. `selected` is how many nodes are selected in all.
    void drawPolygons(const Scene& scene, std::span<const CollisionPolygonRef> polygons,
                      usize selected);

    /// The items of the surface types' group menu, for `layer`, an index into
    /// `Scene::layers`.
    void drawSurfaceTypesMenu(const Scene& scene, usize layer);

private:
    struct PolygonSelection {
        usize layer = 0;
        usize gameLayer = 0;
        std::vector<usize> polygons;
    };

    void drawPolygonSection(const Collision& collision, const PolygonSelection& selection);
    void drawSurfaceSection(const Scene& scene, const SceneLayer& layer, const Collision& collision,
                            const PolygonSelection& selection, u16 type);
    void drawSurfaceChoice(const Collision& collision, const PolygonSelection& selection, u16 type);
    void drawSurfaceRows(const Scene& scene, const SceneLayer& layer, const Collision& collision,
                         const PolygonSelection& selection, u16 type);
    [[nodiscard]] SurfaceFields drawSurfaceLinks(const Scene& scene, const SceneLayer& layer,
                                                 const Collision& collision,
                                                 const PolygonSelection& selection,
                                                 const SurfaceFields& fields,
                                                 const IgnoredSettings& ignored);
    [[nodiscard]] SurfaceFields drawSurfaceNamedRows(const SurfaceFields& fields,
                                                     const IgnoredSettings& ignored);
    [[nodiscard]] SurfaceFields drawSurfaceOtherRows(const SurfaceFields& fields,
                                                     const IgnoredSettings& ignored);
    [[nodiscard]] std::optional<s64> drawNamedField(std::string_view label, std::string_view help,
                                                    SurfaceSetting setting, s64 value, s64 maximum,
                                                    std::string_view warning = {});
    void pickSurfaceType(const PolygonSelection& selection, u16 type, const SurfaceType& surface);

    Pick pick;
    const CollisionNames* collisionNames = nullptr;
    CollisionRows collisionRows;
};

} // namespace toki::scene
