#include "PathEdits.h"

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
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize PathSize = 0x08;
constexpr usize PathPointSize = 6;

/// The path list one header points at.
struct PathSite {
    std::string header;
    usize command = 0;
    std::string list;
    usize count = 0;
};

[[nodiscard]] base::Diagnostic makeEditError(std::string message) {
    return base::makeError({}, std::move(message));
}

[[nodiscard]] std::string makeHeaderPrefix(const SceneModel& model, const usize layer) {
    return std::format("{}_header{:02}", model.getImages().front().front().name, layer);
}

[[nodiscard]] std::vector<u8> makePointBytes(const std::span<const std::array<s16, 3>> points) {
    std::vector<u8> bytes;
    bytes.reserve(points.size() * PathPointSize);
    for (const std::array<s16, 3>& point : points) {
        for (const s16 value : point) {
            const auto word = static_cast<u16>(value);
            bytes.push_back(static_cast<u8>(word >> 8));
            bytes.push_back(static_cast<u8>(word));
        }
    }
    return bytes;
}

[[nodiscard]] std::expected<void, base::Diagnostic>
requirePointCount(const std::span<const std::array<s16, 3>> points) {
    if (points.empty()) {
        return std::unexpected(makeEditError("a path keeps at least one point"));
    }
    if (points.size() > MaximumPathPoints) {
        return std::unexpected(makeEditError(std::format("a path holds at most {} points, not {}",
                                                         MaximumPathPoints, points.size())));
    }
    return {};
}

[[nodiscard]] std::optional<PathSite> findIn(const SceneModel& model, const std::string& header) {
    const z64::SymbolData* commands = model.findSymbol(header);
    const std::optional<usize> command =
        commands != nullptr ? findCommand(*commands, CommandCode::PathList) : std::nullopt;
    const std::optional<std::string> list =
        command ? findPointee(*commands, *command + HeaderPointerSize) : std::nullopt;
    const z64::SymbolData* paths = list ? model.findSymbol(*list) : nullptr;
    if (paths == nullptr) {
        return std::nullopt;
    }
    return PathSite{.header = header,
                    .command = *command,
                    .list = *list,
                    .count = paths->bytes.size() / PathSize};
}

[[nodiscard]] std::expected<void, base::Diagnostic> repoint(SceneModel& model,
                                                            const std::string& symbol,
                                                            const usize offset,
                                                            const std::string& target) {
    z64::SymbolData pointing = *model.findSymbol(symbol);
    std::ranges::find(pointing.relocations, offset, &z64::Relocation::offset)->symbol = target;
    return model.setSymbol(std::move(pointing));
}

/// `name` copied as a symbol of the scene called `base` or the first free
/// name after it, and the word at `offset` of `pointing` made to point at the
/// copy. Returns the copy's name.
[[nodiscard]] std::expected<std::string, base::Diagnostic>
copyFor(SceneModel& model, const std::string& name, const std::string& base,
        const std::string& pointing, const usize offset) {
    z64::SymbolData copy = *model.findSymbol(name);
    copy.name = makeFreeName(model, base);
    copy.startsFile = false;
    std::string copied = copy.name;
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(0, std::move(copy)); !added) {
        return std::unexpected(added.error());
    }
    if (std::expected<void, base::Diagnostic> set = repoint(model, pointing, offset, copied);
        !set) {
        return std::unexpected(set.error());
    }
    return copied;
}

/// The layer's path list, the header and the list made the layer's own.
[[nodiscard]] std::expected<PathSite, base::Diagnostic> ownPaths(SceneModel& model,
                                                                 const usize layer) {
    const std::expected<std::string, base::Diagnostic> header =
        ownHeader(model, {.image = 0, .layer = layer});
    if (!header) {
        return std::unexpected(header.error());
    }
    std::optional<PathSite> found = findIn(model, *header);
    if (!found) {
        return std::unexpected(makeEditError(std::format("'{}' has no path list", *header)));
    }
    if (countPointers(model, found->list) > 1) {
        std::expected<std::string, base::Diagnostic> copied =
            copyFor(model, found->list, makeHeaderPrefix(model, layer) + "_pathList", found->header,
                    found->command + HeaderPointerSize);
        if (!copied) {
            return std::unexpected(copied.error());
        }
        found->list = std::move(*copied);
    }
    return *found;
}

/// The points of the path at `index` of `site`, made the list's own.
[[nodiscard]] std::expected<std::string, base::Diagnostic>
ownPoints(SceneModel& model, const PathSite& site, const usize index) {
    const usize offset = index * PathSize + HeaderPointerSize;
    const std::optional<std::string> points = findPointee(*model.findSymbol(site.list), offset);
    if (!points || model.findSymbol(*points) == nullptr) {
        return std::unexpected(
            makeEditError(std::format("path {} of '{}' points at no points", index, site.list)));
    }
    if (countPointers(model, *points) <= 1) {
        return *points;
    }
    return copyFor(model, *points, std::format("{}_points{}", site.list, index), site.list, offset);
}

[[nodiscard]] std::expected<PathSite, base::Diagnostic>
createPaths(SceneModel& model, const usize layer, const std::string& header) {
    z64::SymbolData list{.name = makeFreeName(model, makeHeaderPrefix(model, layer) + "_pathList"),
                         .type = "Path",
                         .isArray = true,
                         .alignment = 4};
    z64::SymbolData commands = *model.findSymbol(header);
    const std::optional<usize> command = insertCommand(commands, CommandCode::PathList);
    if (!command) {
        return std::unexpected(makeEditError(
            std::format("'{}' has no SCENE_CMD_END to add a path list before", header)));
    }
    commands.relocations.push_back({.offset = *command + HeaderPointerSize, .symbol = list.name});
    std::ranges::sort(commands.relocations, {}, &z64::Relocation::offset);
    PathSite created{.header = header, .command = *command, .list = list.name};
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(0, std::move(list)); !added) {
        return std::unexpected(added.error());
    }
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(commands)); !set) {
        return std::unexpected(set.error());
    }
    return created;
}

} // namespace

std::expected<void, base::Diagnostic>
setPathPoints(SceneModel& model, const usize layer, const usize index,
              const std::span<const std::array<s16, 3>> points) {
    if (std::expected<void, base::Diagnostic> counted = requirePointCount(points); !counted) {
        return counted;
    }
    const std::expected<PathSite, base::Diagnostic> site = ownPaths(model, layer);
    if (!site) {
        return std::unexpected(site.error());
    }
    if (index >= site->count) {
        return std::unexpected(makeEditError(std::format(
            "'{}' has {} paths, so there is none at {}", site->list, site->count, index)));
    }
    const std::expected<std::string, base::Diagnostic> owned = ownPoints(model, *site, index);
    if (!owned) {
        return std::unexpected(owned.error());
    }
    z64::SymbolData pointBytes = *model.findSymbol(*owned);
    pointBytes.bytes = makePointBytes(points);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(pointBytes)); !set) {
        return set;
    }
    z64::SymbolData list = *model.findSymbol(site->list);
    list.bytes[index * PathSize] = static_cast<u8>(points.size());
    return model.setSymbol(std::move(list));
}

std::expected<usize, base::Diagnostic> addPath(SceneModel& model, const usize layer,
                                               const std::span<const std::array<s16, 3>> points) {
    if (std::expected<void, base::Diagnostic> counted = requirePointCount(points); !counted) {
        return std::unexpected(counted.error());
    }
    const std::expected<std::string, base::Diagnostic> header =
        ownHeader(model, {.image = 0, .layer = layer});
    if (!header) {
        return std::unexpected(header.error());
    }
    const std::expected<PathSite, base::Diagnostic> site =
        findIn(model, *header) ? ownPaths(model, layer) : createPaths(model, layer, *header);
    if (!site) {
        return std::unexpected(site.error());
    }
    z64::SymbolData pointBytes{
        .name = makeFreeName(model, std::format("{}_points{}", site->list, site->count)),
        .type = "Vec3s",
        .isArray = true,
        .alignment = 2,
        .bytes = makePointBytes(points)};
    z64::SymbolData list = *model.findSymbol(site->list);
    const usize at = site->count * PathSize;
    insertBytes(list, at, PathSize);
    list.bytes[at] = static_cast<u8>(points.size());
    list.relocations.push_back({.offset = at + HeaderPointerSize, .symbol = pointBytes.name});
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(0, std::move(pointBytes));
        !added) {
        return std::unexpected(added.error());
    }
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(list)); !set) {
        return std::unexpected(set.error());
    }
    return site->count;
}

std::expected<void, base::Diagnostic> removePath(SceneModel& model, const usize layer,
                                                 const usize index) {
    const std::expected<PathSite, base::Diagnostic> site = ownPaths(model, layer);
    if (!site) {
        return std::unexpected(site.error());
    }
    if (index >= site->count) {
        return std::unexpected(makeEditError(std::format(
            "'{}' has {} paths, so there is none at {}", site->list, site->count, index)));
    }
    z64::SymbolData list = *model.findSymbol(site->list);
    const std::optional<std::string> points =
        findPointee(list, index * PathSize + HeaderPointerSize);
    if (site->count == 1) {
        z64::SymbolData header = *model.findSymbol(site->header);
        eraseBytes(header, site->command, HeaderCommandSize);
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(header)); !set) {
            return set;
        }
        if (countPointers(model, site->list) == 0) {
            if (std::expected<void, base::Diagnostic> removed = model.removeSymbol(site->list);
                !removed) {
                return removed;
            }
        }
    } else {
        eraseBytes(list, index * PathSize, PathSize);
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(list)); !set) {
            return set;
        }
    }
    if (points && model.findSymbol(*points) != nullptr && countPointers(model, *points) == 0) {
        return model.removeSymbol(*points);
    }
    return {};
}

} // namespace toki::scene
