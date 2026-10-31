#include "GeometryChanges.h"

#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneModel.h"
#include "z64/SymbolData.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize GfxSize = 8;

enum class SymbolKind : u8 {
    Vertices,
    List,
    Bounds,
    Collision,
    Other,
};

[[nodiscard]] SymbolKind findKind(const std::string_view name, const std::string_view type) {
    if (type == "Vtx") {
        return SymbolKind::Vertices;
    }
    if (type == "Gfx") {
        return SymbolKind::List;
    }
    if (type.find("RoomShapeCullableEntry") != std::string_view::npos ||
        type.find("RoomShapeDListsEntry") != std::string_view::npos) {
        return SymbolKind::Bounds;
    }
    const bool isCollisionVertices =
        type == "Vec3s" && (name.find("ollision") != std::string_view::npos ||
                            name.find("vtx") != std::string_view::npos);
    if (type == "CollisionPoly" || type == "CollisionHeader" || isCollisionVertices) {
        return SymbolKind::Collision;
    }
    return SymbolKind::Other;
}

[[nodiscard]] std::map<std::string, const z64::SymbolData*>
mapSymbols(const std::vector<std::vector<z64::SymbolData>>& images) {
    std::map<std::string, const z64::SymbolData*> symbols;
    for (const std::vector<z64::SymbolData>& image : images) {
        for (const z64::SymbolData& symbol : image) {
            symbols.emplace(symbol.name, &symbol);
        }
    }
    return symbols;
}

[[nodiscard]] std::optional<GeometryChangeKind>
findChange(const SymbolKind kind, const z64::SymbolData* before, const z64::SymbolData* after) {
    if (before == nullptr) {
        return GeometryChangeKind::Added;
    }
    if (after == nullptr) {
        return GeometryChangeKind::Removed;
    }
    if (before->bytes == after->bytes && before->relocations == after->relocations) {
        return std::nullopt;
    }
    switch (kind) {
    case SymbolKind::Vertices:
        return before->bytes.size() == after->bytes.size() ? GeometryChangeKind::VerticesMoved
                                                           : GeometryChangeKind::Repacked;
    case SymbolKind::List:
        return GeometryChangeKind::Rebuilt;
    case SymbolKind::Bounds:
        return GeometryChangeKind::BoundsUpdated;
    case SymbolKind::Collision:
        return GeometryChangeKind::CollisionUpdated;
    case SymbolKind::Other:
        break;
    }
    return std::nullopt;
}

} // namespace

GeometryChanges listGeometryChanges(const Scene& read, const SceneModel& edited) {
    const SceneModel original(read);
    const std::map<std::string, const z64::SymbolData*> before = mapSymbols(original.getImages());
    const std::map<std::string, const z64::SymbolData*> after = mapSymbols(edited.getImages());
    std::map<std::string, std::pair<const z64::SymbolData*, const z64::SymbolData*>> paired;
    for (const auto& [name, symbol] : before) {
        paired[name].first = symbol;
    }
    for (const auto& [name, symbol] : after) {
        paired[name].second = symbol;
    }
    GeometryChanges listed;
    for (const auto& [name, pair] : paired) {
        const z64::SymbolData* either = pair.second != nullptr ? pair.second : pair.first;
        const SymbolKind kind = findKind(name, either->type);
        if (kind == SymbolKind::Other) {
            continue;
        }
        const std::optional<GeometryChangeKind> change = findChange(kind, pair.first, pair.second);
        if (!change) {
            if (kind == SymbolKind::Vertices || kind == SymbolKind::List) {
                ++listed.unchanged;
            }
            continue;
        }
        const usize sizeBefore = pair.first != nullptr ? pair.first->bytes.size() : 0;
        const usize sizeAfter = pair.second != nullptr ? pair.second->bytes.size() : 0;
        GeometryChange made{.symbol = name,
                            .kind = *change,
                            .byteDelta =
                                static_cast<s64>(sizeAfter) - static_cast<s64>(sizeBefore)};
        if (kind == SymbolKind::List) {
            made.commandsBefore = sizeBefore / GfxSize;
            made.commandsAfter = sizeAfter / GfxSize;
        }
        listed.changes.push_back(std::move(made));
    }
    return listed;
}

const char* getGeometryChangeName(const GeometryChangeKind kind) {
    switch (kind) {
    case GeometryChangeKind::VerticesMoved:
        return "Vertices moved";
    case GeometryChangeKind::Repacked:
        return "Repacked";
    case GeometryChangeKind::Rebuilt:
        return "Rebuilt";
    case GeometryChangeKind::Added:
        return "Added";
    case GeometryChangeKind::Removed:
        return "Removed";
    case GeometryChangeKind::BoundsUpdated:
        return "Bounds updated";
    case GeometryChangeKind::CollisionUpdated:
        return "Collision updated";
    }
    return "";
}

} // namespace toki::scene
