#include "DatabaseReader.h"
#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"

#include <toml++/toml.hpp>

#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr s64 LastSetup = 70;

[[nodiscard]] std::optional<u8> parseSegment(const std::string_view key) {
    constexpr u8 FirstSegment = 0x08;
    constexpr u8 LastSegment = 0x0C;
    unsigned value = 0;
    const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), value);
    if (error != std::errc() || end != key.data() + key.size() || value < FirstSegment ||
        value > LastSegment) {
        return std::nullopt;
    }
    return static_cast<u8>(value);
}

} // namespace

std::vector<ActorPart> DatabaseReader::readParts(const toml::array& parts,
                                                 const std::string_view path) {
    std::vector<ActorPart> read;
    for (usize i = 0; i < parts.size(); ++i) {
        const std::string at = std::format("{}[{}]", path, i);
        const toml::table* table = parts[i].as_table();
        if (table == nullptr) {
            report(parts[i], at, "must be a table");
            continue;
        }
        if (std::optional<ActorPart> part = readPart(*table, at)) {
            read.push_back(std::move(*part));
        }
    }
    return read;
}

std::optional<ActorPart> DatabaseReader::readPart(const toml::table& table,
                                                  const std::string_view path) {
    checkKeys(table, path, PartKeys);
    const bool isList = table.contains("display_list");
    if (isList == table.contains("skeleton")) {
        report(table, path, "must draw either a display_list or a skeleton");
        return std::nullopt;
    }
    ActorPart part{
        .kind = isList ? PartKind::DisplayList : PartKind::Skeleton,
        .symbol = readString(table, isList ? "display_list" : "skeleton", path).value_or(""),
        .object = readString(table, "object", path),
        .pose = readString(table, "pose", path),
        .poseObject = readString(table, "pose_object", path),
        .placement = readPlacement(table, path),
        .isBillboard = readBool(table, "billboard", path).value_or(false),
        .pass = readName(table, "pass", path, PassNames).value_or(Pass::Opaque),
        .isHilit = readBool(table, "hilite", path).value_or(false),
        .location = {.file = file, .line = static_cast<u32>(table.source().begin.line)},
    };
    if (const toml::node* primitive = table.get("prim")) {
        part.colors.primitive = readColor<4>(*primitive, makePath(path, "prim"));
    }
    if (const toml::node* environment = table.get("env")) {
        part.colors.environment = readColor<4>(*environment, makePath(path, "env"));
    }
    if (const toml::table* segments = table.get_as<toml::table>("segments")) {
        part.segments = readSegments(*segments, makePath(path, "segments"));
    }
    if (const std::optional<s64> setup = readInteger(table, "setup", path)) {
        if (*setup < 0 || *setup > LastSetup) {
            report(*table.get("setup"), makePath(path, "setup"),
                   "must be an index of sSetupDL, from 0 to 70");
        } else {
            part.setup = static_cast<u8>(*setup);
        }
    }
    return part;
}

PartPlacement DatabaseReader::readPlacement(const toml::table& table, const std::string_view path) {
    PartPlacement placement;
    placement.translation =
        readVector<3>(table, "translation", path).value_or(placement.translation);
    placement.rotation = readVector<3>(table, "rotation", path).value_or(placement.rotation);
    if (const toml::node* scale = table.get("scale"); scale != nullptr && scale->is_number()) {
        placement.scale.fill(static_cast<f32>(scale->value<double>().value_or(1.0)));
    } else {
        placement.scale = readVector<3>(table, "scale", path).value_or(placement.scale);
    }
    return placement;
}

std::vector<PartSegment> DatabaseReader::readSegments(const toml::table& table,
                                                      const std::string_view path) {
    std::vector<PartSegment> segments;
    for (const auto& [key, node] : table) {
        const std::string at = makePath(path, key.str());
        const std::optional<u8> segment = parseSegment(key.str());
        if (segment && node.is_string()) {
            segments.push_back(
                PartSegment{.segment = *segment, .target = node.value<std::string>().value_or("")});
        } else if (const toml::table* built = node.as_table(); segment && built != nullptr) {
            segments.push_back(
                PartSegment{.segment = *segment, .target = readBuiltList(*built, at)});
        } else {
            report(node, at,
                   "must be a segment from 8 to 12 set to a symbol's name, or to a table of "
                   "what a list built at run time sets");
        }
    }
    return segments;
}

BuiltList DatabaseReader::readBuiltList(const toml::table& table, const std::string_view path) {
    checkKeys(table, path, BuiltListKeys);
    BuiltList built;
    if (const toml::node* primitive = table.get("prim")) {
        built.colors.primitive = readColor<4>(*primitive, makePath(path, "prim"));
    }
    if (const toml::node* environment = table.get("env")) {
        built.colors.environment = readColor<4>(*environment, makePath(path, "env"));
    }
    const toml::node* tiles = table.get("tiles");
    if (tiles == nullptr) {
        return built;
    }
    const toml::array* array = tiles->as_array();
    if (array == nullptr) {
        report(*tiles, makePath(path, "tiles"), "must be a list of tile sizes");
        return built;
    }
    for (usize i = 0; i < array->size(); ++i) {
        if (std::optional<TileSize> tile =
                readTileSize((*array)[i], std::format("{}.tiles[{}]", path, i))) {
            built.tiles.push_back(*tile);
        }
    }
    return built;
}

std::optional<TileSize> DatabaseReader::readTileSize(const toml::node& node,
                                                     const std::string_view path) {
    constexpr s64 LastTile = 7;
    constexpr s64 LastCoordinate = 0xFFF;
    constexpr s64 LargestSide = 1024;
    const toml::table* table = node.as_table();
    if (table == nullptr) {
        report(node, path, "must be a table of tile, x, y, width and height");
        return std::nullopt;
    }
    checkKeys(*table, path, TileSizeKeys);
    const auto readBounded = [&](const std::string_view key, const s64 first, const s64 last,
                                 const std::optional<s64> fallback) -> std::optional<s64> {
        const std::optional<s64> value = readInteger(*table, key, path);
        if (!value && !table->contains(key) && fallback) {
            return fallback;
        }
        if (!value || *value < first || *value > last) {
            report(table->contains(key) ? *table->get(key) : node, makePath(path, key),
                   std::format("must be a whole number from {} to {}", first, last));
            return std::nullopt;
        }
        return value;
    };
    const std::optional<s64> tile = readBounded("tile", 0, LastTile, 0);
    const std::optional<s64> x = readBounded("x", 0, LastCoordinate, 0);
    const std::optional<s64> y = readBounded("y", 0, LastCoordinate, 0);
    const std::optional<s64> width = readBounded("width", 1, LargestSide, std::nullopt);
    const std::optional<s64> height = readBounded("height", 1, LargestSide, std::nullopt);
    if (!tile || !x || !y || !width || !height) {
        return std::nullopt;
    }
    return TileSize{.tile = static_cast<u8>(*tile),
                    .x = static_cast<u16>(*x),
                    .y = static_cast<u16>(*y),
                    .width = static_cast<u16>(*width),
                    .height = static_cast<u16>(*height)};
}

} // namespace toki::actor
