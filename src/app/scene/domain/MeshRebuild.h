#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/MeshEdits.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneHeader.h"
#include "base/Diagnostic.h"
#include "z64/MemoryImage.h"
#include "z64/SymbolData.h"

#include <array>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

class SceneModel;
struct Scene;

/// A symbol added to the model's image numbered `image`: the scene's first,
/// then each room's (`SceneModel::getImages`).
struct AddedSymbol {
    usize image = 0;
    z64::SymbolData symbol;
};

/// What writing drafts of rooms' meshes changes in a scene's model. Each run
/// of vertex loads and triangles whose faces changed is written again in
/// place, its triangles in the order the draft draws them and its vertices
/// loaded afresh 32 at a time from an array of its own; every other command
/// of its list is kept as it was, and every list no draft changed keeps its
/// bytes.
struct MeshRebuild {
    /// Symbols set whole: the lists rebuilt, the arrays their vertices are
    /// loaded from where an array only they loaded from is used again, and
    /// symbols that point into a list past a rebuilt run.
    std::vector<z64::SymbolData> changed;
    std::vector<AddedSymbol> added;
    /// Arrays nothing loads from any more.
    std::vector<std::string> removed;
    /// Normals of entries of unchanged runs whose vertices' faces changed,
    /// and bounds of cullable rooms' parts.
    std::vector<SymbolWrite> writes;
    /// The mesh as reading the written lists reads it, its entries grouped
    /// into the drafts' vertices, for reading it again (`readSceneMesh`).
    SceneMesh kept;
    /// The entries the corners of each draft's faces are loaded from once
    /// written, draft by draft and face by face.
    std::vector<std::vector<std::array<MeshEntryKey, 3>>> faces;
    /// The lists rebuilt, by name.
    std::vector<std::string> lists;

    [[nodiscard]] bool isEmpty() const {
        return changed.empty() && added.empty() && removed.empty() && writes.empty();
    }
};

/// What writing `drafts`, drafts of rooms of `mesh`, changes in `model`,
/// the model of `scene`. Before it is handed back, the rebuild is made in a
/// copy of the model and the lists are run again: a rebuild whose rooms do
/// not draw the drafts' faces is an error, as is one that would have to
/// rebuild a run the mesh does not read all of, or that something points
/// into the middle of.
[[nodiscard]] std::expected<MeshRebuild, base::Diagnostic>
planMeshRebuild(const Scene& scene, const SceneModel& model, const SceneMesh& mesh,
                std::span<const RoomDraft> drafts);

/// Makes `rebuild` in `model`.
[[nodiscard]] std::expected<void, base::Diagnostic> applyMeshRebuild(SceneModel& model,
                                                                     const MeshRebuild& rebuild);

/// `scene`'s layer `layer` with the address of each of its rooms' lists
/// moved to where `images` lays out the symbol it was in, for running the
/// lists of images that were laid out again.
[[nodiscard]] std::vector<RoomHeader>
findMovedRooms(const Scene& scene, usize layer,
               const std::vector<std::shared_ptr<const z64::MemoryImage>>& images);

} // namespace toki::scene
