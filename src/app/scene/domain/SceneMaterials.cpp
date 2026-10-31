#include "SceneMaterials.h"

#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/Scene.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace toki::scene {

namespace {

constexpr std::string_view ListStart = "mat_";
constexpr std::string_view RevertStart = "mat_revert_";
constexpr std::string_view NameStart = "_dl_";
constexpr std::string_view PassStart = "_layer";

[[nodiscard]] bool isMaterialList(const std::string_view name) {
    return name.starts_with(ListStart) && !name.starts_with(RevertStart);
}

[[nodiscard]] bool isTranslucentList(const std::string_view list) {
    const usize pass = list.rfind(PassStart);
    return pass != std::string_view::npos &&
           list.substr(pass + PassStart.size()) != std::string_view("Opaque");
}

[[nodiscard]] std::string makeName(const MeshMaterial& material) {
    if (!material.list.empty()) {
        return findMaterialName(material.list);
    }
    if (!material.texture.empty()) {
        return material.texture;
    }
    return "Untextured";
}

} // namespace

usize SceneMaterial::countFaces() const {
    usize count = 0;
    for (const MaterialRoom& room : rooms) {
        count += room.faces;
    }
    return count;
}

bool SceneMaterial::isListed() const {
    return !rooms.empty() &&
           std::ranges::all_of(rooms, [](const MaterialRoom& room) { return !room.list.empty(); });
}

const MaterialRoom* SceneMaterial::findRoom(const usize room) const {
    const auto found = std::ranges::find(rooms, room, &MaterialRoom::room);
    return found != rooms.end() ? &*found : nullptr;
}

std::string findMaterialName(const std::string_view list) {
    if (!list.starts_with(ListStart)) {
        return std::string(list);
    }
    const std::string_view rest = list.substr(ListStart.size());
    const usize name = rest.find(NameStart);
    const usize pass = rest.rfind(PassStart);
    if (name == std::string_view::npos || pass == std::string_view::npos ||
        pass <= name + NameStart.size()) {
        return std::string(rest);
    }
    return std::string(rest.substr(name + NameStart.size(), pass - name - NameStart.size()));
}

std::string makeMaterialListName(const std::string_view prefix, const std::string_view name,
                                 const bool isTranslucent) {
    return std::format("{}{}{}{}{}{}", ListStart, prefix, NameStart, name, PassStart,
                       isTranslucent ? "Transparent" : "Opaque");
}

std::string renameMaterialList(const std::string_view list, const std::string_view name) {
    const std::string old = findMaterialName(list);
    const usize at = list.find(old, ListStart.size());
    if (!list.starts_with(ListStart) || at == std::string_view::npos) {
        return std::format("{}{}", ListStart, name);
    }
    return std::format("{}{}{}", list.substr(0, at), name, list.substr(at + old.size()));
}

std::vector<SceneMaterial> listSceneMaterials(const Scene& scene, const SceneMesh& mesh) {
    std::vector<SceneMaterial> materials;
    std::map<std::tuple<u64, std::string, bool>, usize> indices;
    std::set<std::string> drawnLists;
    for (const RoomMesh& room : mesh.rooms) {
        for (const MeshFace& face : room.faces) {
            const std::string name = makeName(face.material);
            const auto [found, isNew] = indices.emplace(
                std::tuple{face.material.content, name, face.isTranslucent}, materials.size());
            if (isNew) {
                materials.push_back(SceneMaterial{.name = name,
                                                  .content = face.material.content,
                                                  .isTranslucent = face.isTranslucent,
                                                  .isLit = face.isLit,
                                                  .texture = face.material.texture});
            }
            SceneMaterial& material = materials[found->second];
            auto used = std::ranges::find(material.rooms, room.room, &MaterialRoom::room);
            if (used == material.rooms.end()) {
                material.rooms.push_back(MaterialRoom{
                    .room = room.room,
                    .list = face.material.list,
                    .sample = face.draw.list.empty() ? std::nullopt : std::optional(face.draw)});
                used = std::prev(material.rooms.end());
            }
            ++used->faces;
            if (!face.material.list.empty()) {
                drawnLists.insert(face.material.list);
            }
        }
    }
    for (usize image = FirstRoomImage; image < scene.images.size(); ++image) {
        for (const auto& [name, symbol] : scene.images[image]->symbols) {
            if (symbol.type != "Gfx" || !isMaterialList(name) || drawnLists.contains(name)) {
                continue;
            }
            const std::string material = findMaterialName(name);
            const bool isTranslucent = isTranslucentList(name);
            const auto [found, isNew] =
                indices.emplace(std::tuple{u64{0}, material, isTranslucent}, materials.size());
            if (isNew) {
                materials.push_back(
                    SceneMaterial{.name = material, .isTranslucent = isTranslucent, .isLit = true});
            }
            materials[found->second].rooms.push_back(
                MaterialRoom{.room = image - FirstRoomImage, .list = name});
        }
    }
    std::ranges::stable_sort(materials,
                             [](const SceneMaterial& first, const SceneMaterial& second) {
                                 return std::tuple(first.isTranslucent, first.name) <
                                        std::tuple(second.isTranslucent, second.name);
                             });
    return materials;
}

bool isDrawnUnder(const SceneMaterial& material, const MeshFace& face) {
    return material.content == face.material.content &&
           material.isTranslucent == face.isTranslucent && material.name == makeName(face.material);
}

std::optional<usize> findFaceMaterial(const std::vector<SceneMaterial>& materials,
                                      const MeshFace& face) {
    const auto found = std::ranges::find_if(
        materials, [&face](const SceneMaterial& material) { return isDrawnUnder(material, face); });
    return found != materials.end() ? std::optional(static_cast<usize>(found - materials.begin()))
                                    : std::nullopt;
}

} // namespace toki::scene
