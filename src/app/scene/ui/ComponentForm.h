#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/ComponentSummary.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

struct SceneMaterial;

/// What a person changed in the component form this frame, once the input
/// that changes it is let go.
struct ComponentEdit {
    std::optional<glm::vec3> middle;
    std::optional<std::array<u8, 3>> color;
    std::optional<u8> alpha;
    std::optional<usize> material;
    std::optional<bool> isDrawn;
    std::optional<bool> isSolid;
    /// An index into `SurfaceRows::names`.
    std::optional<usize> collisionMaterial;
};

/// The picked faces as one surface with the collision: how many a room
/// draws and how many of those are solid, how many polygons no face has are
/// picked, and the collision materials they can be made of.
struct SurfaceRows {
    usize drawn = 0;
    usize solid = 0;
    usize solidOnly = 0;
    /// What every solid one is made of, an index into `names`, where they
    /// agree.
    std::optional<usize> material;
    std::vector<std::string> names;
};

/// The Properties panel's form of the picked faces and vertices of the rooms'
/// meshes and of the polygons no face has: their vertices, their faces, and
/// whether they are drawn and solid.
class ComponentForm {
public:
    /// `surface` is nothing when the layer has no collision.
    [[nodiscard]] ComponentEdit draw(const ComponentSummary& summary,
                                     const std::vector<SceneMaterial>& materials,
                                     const std::optional<SurfaceRows>& surface);

private:
    static void drawSurface(const SurfaceRows& surface, ComponentEdit& edit);
    void drawVertices(const ComponentSummary& summary, ComponentEdit& edit);
    static void drawFaces(const ComponentSummary& summary,
                          const std::vector<SceneMaterial>& materials, ComponentEdit& edit);

    /// What the inputs hold while one is held, so that a drag is one edit.
    glm::vec3 middle{0.0F};
    std::array<f32, 3> color{};
    s32 alpha = 0;
    bool isMiddleHeld = false;
    bool isColorHeld = false;
    bool isAlphaHeld = false;
};

} // namespace toki::scene
