#include "CollisionEdits.h"

#include "app/scene/Types.h"
#include "app/scene/domain/HeaderSymbols.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize VertexCountOffset = 0x0C;
constexpr usize VerticesOffset = 0x10;
constexpr usize PolygonCountOffset = 0x14;
constexpr usize PolygonsOffset = 0x18;
constexpr usize SurfaceTypesOffset = 0x1C;
constexpr usize CamerasOffset = 0x20;
constexpr usize WaterBoxCountOffset = 0x24;
constexpr usize WaterBoxesOffset = 0x28;
constexpr usize PolygonSize = 0x10;
constexpr usize VertexSize = 6;
constexpr usize SurfaceTypeSize = 8;
constexpr usize CameraSize = 8;
constexpr usize WaterBoxSize = 0x10;
constexpr u32 VertexFlagsShift = 13;

/// What each list a collision header points at is called in a copy, as
/// fast64 names them after the scene.
struct CollisionList {
    usize offset = 0;
    std::string_view suffix;
};

constexpr std::array<CollisionList, 5> CollisionLists{{
    {.offset = VerticesOffset, .suffix = "_vertices"},
    {.offset = PolygonsOffset, .suffix = "_polygons"},
    {.offset = SurfaceTypesOffset, .suffix = "_polygonTypes"},
    {.offset = CamerasOffset, .suffix = "_bgCamInfo"},
    {.offset = WaterBoxesOffset, .suffix = "_waterBoxes"},
}};

[[nodiscard]] base::Diagnostic makeEditError(std::string message) {
    return base::makeError({}, std::move(message));
}

void writeHalf(std::vector<u8>& bytes, const usize offset, const u16 value) {
    bytes[offset] = static_cast<u8>(value >> 8);
    bytes[offset + 1] = static_cast<u8>(value);
}

void writeWord(std::vector<u8>& bytes, const usize offset, const u32 value) {
    writeHalf(bytes, offset, static_cast<u16>(value >> 16));
    writeHalf(bytes, offset + 2, static_cast<u16>(value));
}

[[nodiscard]] u16 readHalf(const std::vector<u8>& bytes, const usize offset) {
    return static_cast<u16>((bytes[offset] << 8) | bytes[offset + 1]);
}

[[nodiscard]] std::string makeHeaderPrefix(const SceneModel& model, const usize layer) {
    return std::format("{}_header{:02}", model.getImages().front().front().name, layer);
}

/// Where the scene's header for a layer points at its collision header.
struct CollisionSite {
    std::string sceneHeader;
    usize command = 0;
    std::string header;
};

[[nodiscard]] std::expected<CollisionSite, base::Diagnostic>
findCollisionSite(const SceneModel& model, const usize layer) {
    const std::expected<std::string, base::Diagnostic> sceneHeader =
        findHeaderName(model, {.image = 0, .layer = layer});
    if (!sceneHeader) {
        return std::unexpected(sceneHeader.error());
    }
    const z64::SymbolData* commands = model.findSymbol(*sceneHeader);
    const std::optional<usize> command =
        commands != nullptr ? findCommand(*commands, CommandCode::CollisionHeader) : std::nullopt;
    const std::optional<std::string> header =
        command ? findPointee(*commands, *command + HeaderPointerSize) : std::nullopt;
    if (!header || model.findSymbol(*header) == nullptr) {
        return std::unexpected(
            makeEditError(std::format("'{}' points at no collision header", *sceneHeader)));
    }
    return CollisionSite{.sceneHeader = *sceneHeader, .command = *command, .header = *header};
}

/// The list the layer's collision header points at, at `offset` of it.
[[nodiscard]] std::expected<z64::SymbolData, base::Diagnostic>
findList(const SceneModel& model, const usize layer, const usize offset,
         const std::string_view noun) {
    const std::expected<CollisionSite, base::Diagnostic> site = findCollisionSite(model, layer);
    if (!site) {
        return std::unexpected(site.error());
    }
    const std::optional<std::string> list = findPointee(*model.findSymbol(site->header), offset);
    const z64::SymbolData* symbol = list ? model.findSymbol(*list) : nullptr;
    if (symbol == nullptr) {
        return std::unexpected(
            makeEditError(std::format("'{}' points at no {} list", site->header, noun)));
    }
    return *symbol;
}

[[nodiscard]] std::expected<void, base::Diagnostic> requireIndex(const z64::SymbolData& list,
                                                                 const usize index,
                                                                 const usize size,
                                                                 const std::string_view noun) {
    if (index >= list.bytes.size() / size) {
        return std::unexpected(
            makeEditError(std::format("'{}' has {} {}s, so there is none at {}", list.name,
                                      list.bytes.size() / size, noun, index)));
    }
    return {};
}

[[nodiscard]] std::expected<void, base::Diagnostic>
setEntry(SceneModel& model, const usize layer, const usize offset, const usize index,
         const usize size, const std::string_view noun, const std::span<const u8> entry) {
    std::expected<z64::SymbolData, base::Diagnostic> list = findList(model, layer, offset, noun);
    if (!list) {
        return std::unexpected(list.error());
    }
    if (std::expected<void, base::Diagnostic> found = requireIndex(*list, index, size, noun);
        !found) {
        return found;
    }
    std::ranges::copy(entry, list->bytes.begin() + static_cast<std::ptrdiff_t>(index * size));
    return model.setSymbol(std::move(*list));
}

[[nodiscard]] std::array<u8, SurfaceTypeSize> makeSurfaceBytes(const SurfaceType& surface) {
    std::vector<u8> bytes(SurfaceTypeSize);
    writeWord(bytes, 0, surface.data[0]);
    writeWord(bytes, 4, surface.data[1]);
    std::array<u8, SurfaceTypeSize> entry{};
    std::ranges::copy(bytes, entry.begin());
    return entry;
}

[[nodiscard]] std::array<u8, WaterBoxSize> makeWaterBoxBytes(const WaterBox& box) {
    std::vector<u8> bytes(WaterBoxSize);
    writeHalf(bytes, 0, static_cast<u16>(box.xMin));
    writeHalf(bytes, 2, static_cast<u16>(box.ySurface));
    writeHalf(bytes, 4, static_cast<u16>(box.zMin));
    writeHalf(bytes, 6, static_cast<u16>(box.xLength));
    writeHalf(bytes, 8, static_cast<u16>(box.zLength));
    writeWord(bytes, 12, box.properties);
    std::array<u8, WaterBoxSize> entry{};
    std::ranges::copy(bytes, entry.begin());
    return entry;
}

/// Which corner's word a flag is held in, and its bit among the flags.
struct FlagPlace {
    usize corner = 0;
    u16 bit = 0;
};

[[nodiscard]] FlagPlace findFlagPlace(const PolygonFlag flag) {
    switch (flag) {
    case PolygonFlag::IgnoreCamera:
        return {.corner = 0, .bit = 1};
    case PolygonFlag::IgnoreEntity:
        return {.corner = 0, .bit = 2};
    case PolygonFlag::IgnoreProjectiles:
        return {.corner = 0, .bit = 4};
    case PolygonFlag::Conveyor:
        return {.corner = 1, .bit = 1};
    }
    return {};
}

/// Changes each of `polygons` with `change`, given the bytes of the list and
/// the offset of the polygon in them.
template <typename Change>
[[nodiscard]] std::expected<void, base::Diagnostic>
changePolygons(SceneModel& model, const usize layer, const std::span<const usize> polygons,
               const Change& change) {
    std::expected<z64::SymbolData, base::Diagnostic> list =
        findList(model, layer, PolygonsOffset, "polygon");
    if (!list) {
        return std::unexpected(list.error());
    }
    for (const usize polygon : polygons) {
        if (std::expected<void, base::Diagnostic> found =
                requireIndex(*list, polygon, PolygonSize, "polygon");
            !found) {
            return found;
        }
        change(list->bytes, polygon * PolygonSize);
    }
    return model.setSymbol(std::move(*list));
}

[[nodiscard]] std::string makeWaterBoxListName(const SceneModel& model, const std::string& header) {
    constexpr std::string_view HeaderSuffix = "_collisionHeader";
    const std::string base =
        header.ends_with(HeaderSuffix)
            ? header.substr(0, header.size() - HeaderSuffix.size()) + "_waterBoxes"
            : header + "_waterBoxes";
    return makeFreeName(model, base);
}

[[nodiscard]] std::expected<void, base::Diagnostic>
setWaterBoxCount(SceneModel& model, const std::string& header, const usize count) {
    z64::SymbolData symbol = *model.findSymbol(header);
    writeHalf(symbol.bytes, WaterBoxCountOffset, static_cast<u16>(count));
    return model.setSymbol(std::move(symbol));
}

/// `name` copied as a symbol of the scene called `base` or the first free
/// name after it. Returns the copy's name.
[[nodiscard]] std::expected<std::string, base::Diagnostic>
copySymbol(SceneModel& model, const std::string& name, const std::string& base) {
    z64::SymbolData copy = *model.findSymbol(name);
    copy.name = makeFreeName(model, base);
    copy.startsFile = false;
    std::string copied = copy.name;
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(0, std::move(copy)); !added) {
        return std::unexpected(added.error());
    }
    return copied;
}

} // namespace

std::expected<void, base::Diagnostic> setSurfaceType(SceneModel& model, const usize layer,
                                                     const usize index,
                                                     const SurfaceType& surface) {
    return setEntry(model, layer, SurfaceTypesOffset, index, SurfaceTypeSize, "surface type",
                    makeSurfaceBytes(surface));
}

std::expected<usize, base::Diagnostic> addSurfaceType(SceneModel& model, const usize layer,
                                                      const SurfaceType& surface) {
    std::expected<z64::SymbolData, base::Diagnostic> list =
        findList(model, layer, SurfaceTypesOffset, "surface type");
    if (!list) {
        return std::unexpected(list.error());
    }
    const usize index = list->bytes.size() / SurfaceTypeSize;
    std::ranges::copy(makeSurfaceBytes(surface), std::back_inserter(list->bytes));
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(*list)); !set) {
        return std::unexpected(set.error());
    }
    return index;
}

std::expected<usize, base::Diagnostic> removeUnusedSurfaceTypes(SceneModel& model,
                                                                const usize layer) {
    std::expected<z64::SymbolData, base::Diagnostic> surfaces =
        findList(model, layer, SurfaceTypesOffset, "surface type");
    std::expected<z64::SymbolData, base::Diagnostic> polygons =
        findList(model, layer, PolygonsOffset, "polygon");
    if (!surfaces || !polygons) {
        return std::unexpected(!surfaces ? surfaces.error() : polygons.error());
    }
    const usize count = surfaces->bytes.size() / SurfaceTypeSize;
    std::vector<bool> isUsed(count, false);
    for (usize offset = 0; offset + PolygonSize <= polygons->bytes.size(); offset += PolygonSize) {
        const u16 type = readHalf(polygons->bytes, offset);
        if (type < count) {
            isUsed[type] = true;
        }
    }
    if (std::ranges::none_of(isUsed, std::identity{}) && count > 0) {
        isUsed[0] = true;
    }
    std::vector<u16> moved(count, 0);
    std::vector<u8> kept;
    for (usize i = 0; i < count; ++i) {
        if (isUsed[i]) {
            moved[i] = static_cast<u16>(kept.size() / SurfaceTypeSize);
            const auto entry =
                surfaces->bytes.begin() + static_cast<std::ptrdiff_t>(i * SurfaceTypeSize);
            kept.insert(kept.end(), entry, entry + SurfaceTypeSize);
        }
    }
    const usize removed = count - kept.size() / SurfaceTypeSize;
    if (removed == 0) {
        return 0;
    }
    for (usize offset = 0; offset + PolygonSize <= polygons->bytes.size(); offset += PolygonSize) {
        const u16 type = readHalf(polygons->bytes, offset);
        if (type < count) {
            writeHalf(polygons->bytes, offset, moved[type]);
        }
    }
    surfaces->bytes = std::move(kept);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(*surfaces)); !set) {
        return std::unexpected(set.error());
    }
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(*polygons)); !set) {
        return std::unexpected(set.error());
    }
    return removed;
}

std::expected<void, base::Diagnostic> setPolygonSurfaceType(SceneModel& model, const usize layer,
                                                            const std::span<const usize> polygons,
                                                            const u16 type) {
    const std::expected<z64::SymbolData, base::Diagnostic> surfaces =
        findList(model, layer, SurfaceTypesOffset, "surface type");
    if (!surfaces) {
        return std::unexpected(surfaces.error());
    }
    if (std::expected<void, base::Diagnostic> found =
            requireIndex(*surfaces, type, SurfaceTypeSize, "surface type");
        !found) {
        return found;
    }
    return changePolygons(model, layer, polygons, [type](std::vector<u8>& bytes, const usize at) {
        writeHalf(bytes, at, type);
    });
}

std::expected<void, base::Diagnostic> setPolygonFlag(SceneModel& model, const usize layer,
                                                     const std::span<const usize> polygons,
                                                     const PolygonFlag flag, const bool isSet) {
    const FlagPlace place = findFlagPlace(flag);
    const auto mask = static_cast<u16>(place.bit << VertexFlagsShift);
    return changePolygons(
        model, layer, polygons, [&place, mask, isSet](std::vector<u8>& bytes, const usize at) {
            const usize word = at + 2 + (place.corner * 2);
            const u16 data = readHalf(bytes, word);
            writeHalf(bytes, word, static_cast<u16>(isSet ? data | mask : data & ~mask));
        });
}

bool hasPolygonFlag(const CollisionPolygon& polygon, const PolygonFlag flag) {
    const FlagPlace place = findFlagPlace(flag);
    return (polygon.vertexData[place.corner] & (place.bit << VertexFlagsShift)) != 0;
}

CollisionPolygon makeFlaggedPolygon(CollisionPolygon polygon, const PolygonFlag flag,
                                    const bool isSet) {
    const FlagPlace place = findFlagPlace(flag);
    const auto mask = static_cast<u16>(place.bit << VertexFlagsShift);
    u16& data = polygon.vertexData[place.corner];
    data = static_cast<u16>(isSet ? data | mask : data & ~mask);
    return polygon;
}

std::expected<void, base::Diagnostic> setCollisionGeometry(SceneModel& model, const usize layer,
                                                           const Collision& collision) {
    if (collision.vertices.size() > MaximumCollisionVertices) {
        return std::unexpected(makeEditError(
            std::format("the collision would have {} vertices, and a polygon can name {} at most",
                        collision.vertices.size(), MaximumCollisionVertices)));
    }
    const std::expected<CollisionSite, base::Diagnostic> site = findCollisionSite(model, layer);
    if (!site) {
        return std::unexpected(site.error());
    }
    std::expected<z64::SymbolData, base::Diagnostic> vertices =
        findList(model, layer, VerticesOffset, "vertex");
    std::expected<z64::SymbolData, base::Diagnostic> polygons =
        findList(model, layer, PolygonsOffset, "polygon");
    if (!vertices || !polygons) {
        return std::unexpected(!vertices ? vertices.error() : polygons.error());
    }
    vertices->bytes.assign(collision.vertices.size() * VertexSize, 0);
    for (usize i = 0; i < collision.vertices.size(); ++i) {
        for (usize axis = 0; axis < 3; ++axis) {
            writeHalf(vertices->bytes, (i * VertexSize) + (axis * 2),
                      static_cast<u16>(collision.vertices[i][axis]));
        }
    }
    polygons->bytes.assign(collision.polygons.size() * PolygonSize, 0);
    for (usize i = 0; i < collision.polygons.size(); ++i) {
        const CollisionPolygon& polygon = collision.polygons[i];
        const usize at = i * PolygonSize;
        writeHalf(polygons->bytes, at, polygon.type);
        for (usize corner = 0; corner < 3; ++corner) {
            writeHalf(polygons->bytes, at + 2 + (corner * 2), polygon.vertexData[corner]);
            writeHalf(polygons->bytes, at + 8 + (corner * 2),
                      static_cast<u16>(polygon.normal[corner]));
        }
        writeHalf(polygons->bytes, at + 14, static_cast<u16>(polygon.distance));
    }
    z64::SymbolData header = *model.findSymbol(site->header);
    for (usize axis = 0; axis < 3; ++axis) {
        writeHalf(header.bytes, axis * 2, static_cast<u16>(collision.minBounds[axis]));
        writeHalf(header.bytes, 6 + (axis * 2), static_cast<u16>(collision.maxBounds[axis]));
    }
    writeHalf(header.bytes, VertexCountOffset, static_cast<u16>(collision.vertices.size()));
    writeHalf(header.bytes, PolygonCountOffset, static_cast<u16>(collision.polygons.size()));
    for (z64::SymbolData* list : {&*vertices, &*polygons, &header}) {
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(*list)); !set) {
            return set;
        }
    }
    return {};
}

std::expected<void, base::Diagnostic> setWaterBox(SceneModel& model, const usize layer,
                                                  const usize index, const WaterBox& box) {
    return setEntry(model, layer, WaterBoxesOffset, index, WaterBoxSize, "waterbox",
                    makeWaterBoxBytes(box));
}

std::expected<usize, base::Diagnostic> addWaterBox(SceneModel& model, const usize layer,
                                                   const WaterBox& box) {
    const std::expected<CollisionSite, base::Diagnostic> site = findCollisionSite(model, layer);
    if (!site) {
        return std::unexpected(site.error());
    }
    z64::SymbolData header = *model.findSymbol(site->header);
    const std::optional<std::string> listName = findPointee(header, WaterBoxesOffset);
    if (!listName) {
        z64::SymbolData list{.name = makeWaterBoxListName(model, site->header),
                             .type = "WaterBox",
                             .isArray = true,
                             .alignment = 4};
        std::ranges::copy(makeWaterBoxBytes(box), std::back_inserter(list.bytes));
        header.relocations.push_back({.offset = WaterBoxesOffset, .symbol = list.name});
        std::ranges::sort(header.relocations, {}, &z64::Relocation::offset);
        writeWord(header.bytes, WaterBoxesOffset, 0);
        writeHalf(header.bytes, WaterBoxCountOffset, 1);
        if (std::expected<void, base::Diagnostic> added = model.addSymbol(0, std::move(list));
            !added) {
            return std::unexpected(added.error());
        }
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(header)); !set) {
            return std::unexpected(set.error());
        }
        return 0;
    }
    z64::SymbolData list = *model.findSymbol(*listName);
    const usize index = list.bytes.size() / WaterBoxSize;
    if (index >= MaximumWaterBoxes) {
        return std::unexpected(makeEditError(
            std::format("'{}' holds the most waterboxes a header counts", list.name)));
    }
    std::ranges::copy(makeWaterBoxBytes(box), std::back_inserter(list.bytes));
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(list)); !set) {
        return std::unexpected(set.error());
    }
    if (std::expected<void, base::Diagnostic> set =
            setWaterBoxCount(model, site->header, index + 1);
        !set) {
        return std::unexpected(set.error());
    }
    return index;
}

std::expected<void, base::Diagnostic> removeWaterBox(SceneModel& model, const usize layer,
                                                     const usize index) {
    const std::expected<CollisionSite, base::Diagnostic> site = findCollisionSite(model, layer);
    if (!site) {
        return std::unexpected(site.error());
    }
    std::expected<z64::SymbolData, base::Diagnostic> list =
        findList(model, layer, WaterBoxesOffset, "waterbox");
    if (!list) {
        return std::unexpected(list.error());
    }
    if (std::expected<void, base::Diagnostic> found =
            requireIndex(*list, index, WaterBoxSize, "waterbox");
        !found) {
        return found;
    }
    const usize count = list->bytes.size() / WaterBoxSize - 1;
    if (count > 0) {
        eraseBytes(*list, index * WaterBoxSize, WaterBoxSize);
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(*list)); !set) {
            return set;
        }
        return setWaterBoxCount(model, site->header, count);
    }
    z64::SymbolData header = *model.findSymbol(site->header);
    std::erase_if(header.relocations, [](const z64::Relocation& relocation) {
        return relocation.offset == WaterBoxesOffset;
    });
    writeWord(header.bytes, WaterBoxesOffset, 0);
    writeHalf(header.bytes, WaterBoxCountOffset, 0);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(header)); !set) {
        return set;
    }
    if (countPointers(model, list->name) == 0) {
        return model.removeSymbol(list->name);
    }
    return {};
}

std::expected<void, base::Diagnostic> setCameraSetting(SceneModel& model, const usize layer,
                                                       const usize index, const u16 setting) {
    std::expected<z64::SymbolData, base::Diagnostic> list =
        findList(model, layer, CamerasOffset, "camera");
    if (!list) {
        return std::unexpected(list.error());
    }
    if (std::expected<void, base::Diagnostic> found =
            requireIndex(*list, index, CameraSize, "camera");
        !found) {
        return found;
    }
    writeHalf(list->bytes, index * CameraSize, setting);
    return model.setSymbol(std::move(*list));
}

std::expected<void, base::Diagnostic> makeOwnCollision(SceneModel& model, const usize layer) {
    const std::expected<CollisionSite, base::Diagnostic> shared = findCollisionSite(model, layer);
    if (!shared) {
        return std::unexpected(shared.error());
    }
    if (countPointers(model, shared->header) <= 1) {
        return {};
    }
    const std::expected<std::string, base::Diagnostic> sceneHeader =
        ownHeader(model, {.image = 0, .layer = layer});
    if (!sceneHeader) {
        return std::unexpected(sceneHeader.error());
    }
    const std::string prefix = makeHeaderPrefix(model, layer);
    z64::SymbolData header = *model.findSymbol(shared->header);
    for (const CollisionList& list : CollisionLists) {
        const auto relocation =
            std::ranges::find(header.relocations, list.offset, &z64::Relocation::offset);
        if (relocation == header.relocations.end()) {
            continue;
        }
        std::expected<std::string, base::Diagnostic> copied =
            copySymbol(model, relocation->symbol, prefix + std::string(list.suffix));
        if (!copied) {
            return std::unexpected(copied.error());
        }
        relocation->symbol = std::move(*copied);
    }
    header.name = makeFreeName(model, prefix + "_collisionHeader");
    header.startsFile = false;
    const std::string headerName = header.name;
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(0, std::move(header));
        !added) {
        return added;
    }
    z64::SymbolData commands = *model.findSymbol(*sceneHeader);
    const std::optional<usize> command = findCommand(commands, CommandCode::CollisionHeader);
    if (!command) {
        return std::unexpected(
            makeEditError(std::format("'{}' has no collision header command", *sceneHeader)));
    }
    std::ranges::find(commands.relocations, *command + HeaderPointerSize, &z64::Relocation::offset)
        ->symbol = headerName;
    return model.setSymbol(std::move(commands));
}

} // namespace toki::scene
