#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionMaterials.h"
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

/// The Properties panel's settings of a collision material: its surface type,
/// what passes through it and its surface type's settings, each changing
/// every polygon made of it, for the material of the faces picked or one
/// selected in the Scene panel.
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

    /// The settings of the collision material `polygons` are made of, where
    /// they share one. `polygons` are those picked of one layer's collision.
    /// `isBound` says some are of faces whose material is bound, which then
    /// cannot be given a surface type of their own.
    void drawPolygons(const Scene& scene, std::span<const CollisionPolygonRef> polygons,
                      bool isBound);

    /// The settings of `material` of the collision of `layer`, an index into
    /// `Scene::layers`.
    void drawMaterial(const Scene& scene, usize layer, const CollisionMaterial& material);

    /// What the material whose settings were shown last is once an edit of
    /// them was made, if one was since this was asked last.
    [[nodiscard]] std::optional<CollisionMaterial> takeMovedMaterial() {
        return std::exchange(movedMaterial, std::nullopt);
    }

    /// The items of the surface types' group menu, for `layer`, an index into
    /// `Scene::layers`.
    void drawSurfaceTypesMenu(const Scene& scene, usize layer);

private:
    /// A collision material whose settings are shown, and the polygons
    /// picked of it, which Own Copy gives a surface type of their own.
    struct MaterialSelection {
        usize layer = 0;
        usize gameLayer = 0;
        CollisionMaterial material;
        std::vector<usize> picked;
        bool isBound = false;
    };

    void drawSection(const Scene& scene, const Collision& collision,
                     const MaterialSelection& selection);
    void drawSurfaceTypeRow(const Collision& collision, const MaterialSelection& selection);
    void drawFlagRows(const Collision& collision, const MaterialSelection& selection);
    void drawSurfaceRows(const Scene& scene, const SceneLayer& layer, const Collision& collision,
                         const MaterialSelection& selection);
    [[nodiscard]] SurfaceFields drawSurfaceLinks(const Scene& scene, const SceneLayer& layer,
                                                 const Collision& collision,
                                                 const MaterialSelection& selection,
                                                 const SurfaceFields& fields,
                                                 const IgnoredSettings& ignored);
    [[nodiscard]] SurfaceFields drawSurfaceNamedRows(const SurfaceFields& fields,
                                                     const IgnoredSettings& ignored);
    [[nodiscard]] SurfaceFields drawSurfaceOtherRows(const SurfaceFields& fields,
                                                     const IgnoredSettings& ignored);
    [[nodiscard]] std::optional<s64> drawNamedField(std::string_view label, std::string_view help,
                                                    SurfaceSetting setting, s64 value, s64 maximum,
                                                    std::string_view warning = {});
    void pickSurfaceType(const Collision& collision, const MaterialSelection& selection,
                         const SurfaceType& surface);

    Pick pick;
    std::optional<CollisionMaterial> movedMaterial;
    const CollisionNames* collisionNames = nullptr;
    CollisionRows collisionRows;
};

} // namespace toki::scene
