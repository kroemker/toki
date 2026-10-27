#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/RoomMesh.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

struct Scene;

/// Where a material is drawn in one room: the list of its own that sets it
/// there, if it has one, how many faces it draws, and the command that drew
/// one of them.
struct MaterialRoom {
    usize room = 0;
    std::string list;
    usize faces = 0;
    std::optional<MeshDraw> sample;
};

/// What faces are drawn under, grouped across rooms: faces drawn under the
/// same RDP state and the same name are one material, however many rooms
/// keep a copy of it. Its name is what fast64 named its list, or else the
/// texture its tile was loaded from.
struct SceneMaterial {
    std::string name;
    /// The RDP state as `MeshMaterial::content` holds it, or zero for a list
    /// of its own no face is drawn with yet.
    u64 content = 0;
    bool isTranslucent = false;
    bool isLit = false;
    std::string texture;
    std::vector<MaterialRoom> rooms;

    [[nodiscard]] usize countFaces() const;

    /// Whether every room it is in sets it by a list of its own, which is what
    /// renaming it renames.
    [[nodiscard]] bool isListed() const;

    [[nodiscard]] const MaterialRoom* findRoom(usize room) const;
};

/// The name fast64 gives a material in the name of its list:
/// `mat_<room>_dl_<name>_layer<Pass>` is `<name>`.
[[nodiscard]] std::string findMaterialName(std::string_view list);

/// The list name of a material named `name` in the room whose lists'
/// names start `prefix`, for `pass`, as fast64 writes it.
[[nodiscard]] std::string makeMaterialListName(std::string_view prefix, std::string_view name,
                                               bool isTranslucent);

/// `list` with the material's name in it replaced by `name`.
[[nodiscard]] std::string renameMaterialList(std::string_view list, std::string_view name);

/// The materials `mesh`'s rooms are drawn with, by name, and the lists of
/// their own in `scene`'s rooms that no face is drawn with.
[[nodiscard]] std::vector<SceneMaterial> listSceneMaterials(const Scene& scene,
                                                            const SceneMesh& mesh);

/// The material of `materials` `face` is drawn under, if one is.
[[nodiscard]] std::optional<usize> findFaceMaterial(const std::vector<SceneMaterial>& materials,
                                                    const MeshFace& face);

} // namespace toki::scene
