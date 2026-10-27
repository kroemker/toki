#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/EditMesh.h"
#include "app/scene/domain/MeshEdits.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

struct SceneMaterial;
struct SceneMesh;

/// What the picked faces and vertices of a mesh hold, as the Properties panel
/// shows it: a value where every one of them agrees, nothing where they do
/// not.
struct ComponentSummary {
    usize vertices = 0;
    usize faces = 0;
    glm::vec3 middle{0.0F};
    /// The colour of the unlit entries, and the alpha of every entry.
    std::optional<std::array<u8, 3>> color;
    std::optional<u8> alpha;
    bool hasLit = false;
    bool hasUnlit = false;
    std::optional<usize> material;
    std::optional<bool> isTranslucent;
    std::vector<std::string> vertexArrays;
    std::vector<std::string> lists;
};

/// Sums up `vertices` and `faces` of `mesh`, a room's vertices and faces by
/// their room as their piece, naming the material of the faces from
/// `materials`.
[[nodiscard]] ComponentSummary summarizeComponents(const SceneMesh& mesh,
                                                   const std::vector<SceneMaterial>& materials,
                                                   std::span<const ComponentRef> vertices,
                                                   std::span<const ComponentRef> faces);

/// The writes that give every entry of `vertices` that is not lit `color`,
/// or every entry `alpha`.
[[nodiscard]] std::vector<SymbolWrite>
makeColorWrites(const SceneMesh& mesh, std::span<const ComponentRef> vertices,
                const std::optional<std::array<u8, 3>>& color, std::optional<u8> alpha);

} // namespace toki::scene
