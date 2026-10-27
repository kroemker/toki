#include "MaterialEdits.h"

#include "app/scene/domain/MeshDraft.h"
#include "app/scene/domain/RoomMesh.h"
#include "app/scene/domain/SceneMaterials.h"
#include "app/scene/domain/SceneModel.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] std::expected<void, base::Diagnostic> checkListed(const SceneMaterial& material,
                                                                const std::string_view name) {
    if (!material.isListed()) {
        return std::unexpected(base::makeError(
            {}, std::format("'{}' is not set by a list of its own in every room it is drawn in",
                            material.name)));
    }
    if (name.empty() ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
            std::string_view::npos) {
        return std::unexpected(
            base::makeError({}, std::format("'{}' is not a name C can hold", name)));
    }
    return {};
}

} // namespace

std::expected<std::vector<OperatedRoom>, base::Diagnostic>
assignMaterial(const SceneMesh& mesh, const std::span<const MeshPick> picks,
               const SceneMaterial& material) {
    std::vector<OperatedRoom> operated;
    for (const MeshPick& pick : picks) {
        if (pick.faces.empty()) {
            continue;
        }
        const RoomMesh* room = findRoomMesh(mesh, pick.room);
        if (room == nullptr) {
            continue;
        }
        const MaterialRoom* drawn = material.findRoom(pick.room);
        if (drawn == nullptr || !drawn->sample) {
            return std::unexpected(base::makeError(
                {}, std::format("room {} draws no face with '{}' yet, and a material is not "
                                "copied between rooms",
                                pick.room, material.name)));
        }
        OperatedRoom made{.draft = makeRoomDraft(*room), .faces = pick.faces};
        for (const usize face : pick.faces) {
            if (face < made.draft.faces.size()) {
                made.draft.faces[face].draw = *drawn->sample;
            }
        }
        operated.push_back(std::move(made));
    }
    return operated;
}

std::expected<void, base::Diagnostic>
renameMaterial(SceneModel& model, const SceneMaterial& material, const std::string_view name) {
    if (std::expected<void, base::Diagnostic> checked = checkListed(material, name); !checked) {
        return checked;
    }
    for (const MaterialRoom& room : material.rooms) {
        const z64::SymbolData* old = model.findSymbol(room.list);
        if (old == nullptr) {
            return std::unexpected(
                base::makeError({}, std::format("the scene has no list '{}'", room.list)));
        }
        const std::optional<usize> image = model.findImage(room.list);
        if (!image) {
            return std::unexpected(
                base::makeError({}, std::format("the scene has no list '{}'", room.list)));
        }
        z64::SymbolData renamed = *old;
        renamed.name = renameMaterialList(room.list, name);
        if (model.findSymbol(renamed.name) != nullptr) {
            return std::unexpected(base::makeError(
                {}, std::format("the scene already has a symbol '{}'", renamed.name)));
        }
        if (std::expected<void, base::Diagnostic> added = model.addSymbol(*image, renamed);
            !added) {
            return added;
        }
        std::vector<z64::SymbolData> pointing;
        for (const std::vector<z64::SymbolData>& symbols : model.getImages()) {
            for (const z64::SymbolData& symbol : symbols) {
                z64::SymbolData pointed = symbol;
                bool isPointing = false;
                for (z64::Relocation& relocation : pointed.relocations) {
                    if (relocation.symbol == room.list) {
                        relocation.symbol = renamed.name;
                        isPointing = true;
                    }
                }
                if (isPointing) {
                    pointing.push_back(std::move(pointed));
                }
            }
        }
        for (z64::SymbolData& pointed : pointing) {
            if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(pointed));
                !set) {
                return set;
            }
        }
        if (std::expected<void, base::Diagnostic> removed = model.removeSymbol(room.list);
            !removed) {
            return removed;
        }
    }
    return {};
}

std::expected<void, base::Diagnostic>
duplicateMaterial(SceneModel& model, const SceneMaterial& material, const std::string_view name) {
    if (std::expected<void, base::Diagnostic> checked = checkListed(material, name); !checked) {
        return checked;
    }
    for (const MaterialRoom& room : material.rooms) {
        const z64::SymbolData* old = model.findSymbol(room.list);
        if (old == nullptr) {
            return std::unexpected(
                base::makeError({}, std::format("the scene has no list '{}'", room.list)));
        }
        const std::optional<usize> image = model.findImage(room.list);
        if (!image) {
            return std::unexpected(
                base::makeError({}, std::format("the scene has no list '{}'", room.list)));
        }
        z64::SymbolData copy = *old;
        copy.name = renameMaterialList(room.list, name);
        copy.startsFile = false;
        if (model.findSymbol(copy.name) != nullptr) {
            return std::unexpected(
                base::makeError({}, std::format("the scene already has a symbol '{}'", copy.name)));
        }
        if (std::expected<void, base::Diagnostic> added = model.addSymbol(*image, std::move(copy));
            !added) {
            return added;
        }
    }
    return {};
}

} // namespace toki::scene
