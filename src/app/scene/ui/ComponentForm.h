#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/ComponentSummary.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
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
    std::optional<bool> isLinked;
};

/// The Properties panel's form of the picked faces and vertices of the rooms'
/// meshes: their vertices, their faces, the collision below them and where
/// their bytes are.
class ComponentForm {
public:
    [[nodiscard]] ComponentEdit draw(const ComponentSummary& summary,
                                     const std::vector<SceneMaterial>& materials, bool isLinked);

private:
    void drawVertices(const ComponentSummary& summary, ComponentEdit& edit);
    static void drawFaces(const ComponentSummary& summary,
                          const std::vector<SceneMaterial>& materials, ComponentEdit& edit);
    static void drawSource(const ComponentSummary& summary);

    /// What the inputs hold while one is held, so that a drag is one edit.
    glm::vec3 middle{0.0F};
    std::array<f32, 3> color{};
    s32 alpha = 0;
    bool isMiddleHeld = false;
    bool isColorHeld = false;
    bool isAlphaHeld = false;
};

} // namespace toki::scene
