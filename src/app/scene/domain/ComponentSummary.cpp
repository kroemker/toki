#include "ComponentSummary.h"

#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneMaterials.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize ColorOffset = 12;
constexpr usize AlphaOffset = 15;
constexpr usize VtxSize = 16;

/// Keeps `value` while every one given agrees with it, and nothing after one
/// does not.
template <typename Value>
void agree(std::optional<Value>& kept, bool& isFirst, const Value& value) {
    if (isFirst) {
        kept = value;
        isFirst = false;
    } else if (kept && *kept != value) {
        kept.reset();
    }
}

} // namespace

ComponentSummary summarizeComponents(const SceneMesh& mesh,
                                     const std::vector<SceneMaterial>& materials,
                                     const std::span<const ComponentRef> vertices,
                                     const std::span<const ComponentRef> faces) {
    ComponentSummary summary{.vertices = vertices.size(), .faces = faces.size()};
    std::set<std::string> arrays;
    std::set<std::string> lists;
    bool isFirstColor = true;
    bool isFirstAlpha = true;
    for (const ComponentRef& ref : vertices) {
        const RoomMesh* room = findRoomMesh(mesh, ref.piece);
        if (room == nullptr || ref.index >= room->vertices.size()) {
            continue;
        }
        summary.middle += room->vertices[ref.index].position;
        for (const usize index : room->vertices[ref.index].entries) {
            const MeshEntry& entry = room->entries[index];
            arrays.insert(entry.key.symbol);
            agree(summary.alpha, isFirstAlpha, entry.vtx[AlphaOffset]);
            if (entry.isLit) {
                summary.hasLit = true;
                continue;
            }
            summary.hasUnlit = true;
            agree(summary.color, isFirstColor,
                  std::array{entry.vtx[ColorOffset], entry.vtx[ColorOffset + 1],
                             entry.vtx[ColorOffset + 2]});
        }
    }
    if (!vertices.empty()) {
        summary.middle /= static_cast<f32>(vertices.size());
    }
    bool isFirstMaterial = true;
    bool isFirstPass = true;
    for (const ComponentRef& ref : faces) {
        const RoomMesh* room = findRoomMesh(mesh, ref.piece);
        if (room == nullptr || ref.index >= room->faces.size()) {
            continue;
        }
        const MeshFace& face = room->faces[ref.index];
        if (!face.draw.list.empty()) {
            lists.insert(face.draw.list);
        }
        const std::optional<usize> material = findFaceMaterial(materials, face);
        if (isFirstMaterial) {
            summary.material = material;
            isFirstMaterial = false;
        } else if (summary.material != material) {
            summary.material.reset();
        }
        agree(summary.isTranslucent, isFirstPass, face.isTranslucent);
    }
    summary.vertexArrays.assign(arrays.begin(), arrays.end());
    summary.lists.assign(lists.begin(), lists.end());
    return summary;
}

std::vector<SymbolWrite> makeColorWrites(const SceneMesh& mesh,
                                         const std::span<const ComponentRef> vertices,
                                         const std::optional<std::array<u8, 3>>& color,
                                         const std::optional<u8> alpha) {
    std::vector<SymbolWrite> writes;
    std::set<MeshEntryKey> written;
    for (const ComponentRef& ref : vertices) {
        const RoomMesh* room = findRoomMesh(mesh, ref.piece);
        if (room == nullptr || ref.index >= room->vertices.size()) {
            continue;
        }
        for (const usize index : room->vertices[ref.index].entries) {
            const MeshEntry& entry = room->entries[index];
            if (!written.insert(entry.key).second) {
                continue;
            }
            const usize start = entry.key.index * VtxSize;
            if (color && !entry.isLit) {
                writes.push_back(SymbolWrite{.symbol = entry.key.symbol,
                                             .offset = start + ColorOffset,
                                             .bytes = {(*color)[0], (*color)[1], (*color)[2]}});
            }
            if (alpha) {
                writes.push_back(SymbolWrite{
                    .symbol = entry.key.symbol, .offset = start + AlphaOffset, .bytes = {*alpha}});
            }
        }
    }
    return writes;
}

} // namespace toki::scene
