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

/// The Properties panel's rows for the polygons of the faces picked: what
/// passes through them, set for every one, the settings of the collision
/// material they share, which change every polygon made of it, and what the
/// game reads of the one picked last.
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

    /// `polygons` are those picked of one layer's collision, the one shown
    /// last. `isBound` says some are of faces whose material is bound, whose
    /// collision is then set by the binding alone.
    void drawPolygons(const Scene& scene, std::span<const CollisionPolygonRef> polygons,
                      bool isBound);

    /// The items of the surface types' group menu, for `layer`, an index into
    /// `Scene::layers`.
    void drawSurfaceTypesMenu(const Scene& scene, usize layer);

private:
    struct PolygonSelection {
        usize layer = 0;
        usize gameLayer = 0;
        std::vector<usize> polygons;
    };

    void drawFlagSection(const Collision& collision, const PolygonSelection& selection,
                         bool isBound);
    void drawPolygonSection(const Collision& collision, const PolygonSelection& selection);
    void drawSurfaceSection(const Scene& scene, const SceneLayer& layer, const Collision& collision,
                            const PolygonSelection& selection, bool isBound);
    void drawOwnCopy(const Collision& collision, const PolygonSelection& selection, u16 type,
                     bool isBound);
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
    void pickSurfaceType(const Collision& collision, const PolygonSelection& selection,
                         const SurfaceType& surface);

    Pick pick;
    const CollisionNames* collisionNames = nullptr;
    CollisionRows collisionRows;
};

} // namespace toki::scene
