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

std::vector<SegmentSymbol> DatabaseReader::readSegments(const toml::table& table,
                                                        const std::string_view path) {
    std::vector<SegmentSymbol> segments;
    for (const auto& [key, node] : table) {
        const std::optional<u8> segment = parseSegment(key.str());
        const std::optional<std::string> name = node.value<std::string>();
        if (!segment || !node.is_string() || !name) {
            report(node, makePath(path, key.str()),
                   "must be a segment from 8 to 12 set to a symbol's name");
            continue;
        }
        segments.push_back(SegmentSymbol{.segment = *segment, .name = *name});
    }
    return segments;
}

} // namespace toki::actor
