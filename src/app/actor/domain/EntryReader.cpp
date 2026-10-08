#include "DatabaseReader.h"
#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array SpawnKeys = {std::string_view("age"), std::string_view("time"),
                                  std::string_view("flag")};

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

ActorDatabase DatabaseReader::read(const toml::table& root) && {
    checkKeys(root, {}, DatabaseKeys);
    if (const std::optional<s64> format = readInteger(root, "format", {});
        format && *format != DatabaseFormat) {
        report(*root.get("format"), "format",
               std::format("is {}, and this build reads format {}", *format, DatabaseFormat));
        return std::move(database);
    }
    database.checkedAgainst = readString(root, "checked_against", {});
    const toml::node* actors = root.get("actors");
    if (actors == nullptr) {
        return std::move(database);
    }
    const toml::table* table = actors->as_table();
    if (table == nullptr) {
        report(*actors, "actors", "must be a table of actors");
        return std::move(database);
    }
    for (const auto& [name, node] : *table) {
        actor = name.str();
        if (const toml::table* entry = node.as_table()) {
            database.entries.insert_or_assign(actor, readEntry(actor, *entry));
        } else {
            report(node, {}, "must be a table");
        }
    }
    actor.clear();
    return std::move(database);
}

ActorEntry DatabaseReader::readEntry(const std::string_view name, const toml::table& table) {
    checkKeys(table, {}, EntryKeys);
    ActorEntry entry{
        .actor = std::string(name),
        .location = {.file = file, .line = static_cast<u32>(table.source().begin.line)}};
    entry.status = readName(table, "status", {}, StatusNames).value_or(EntryStatus::Checked);
    if (const toml::table* guessed = table.get_as<toml::table>("guessed_from")) {
        entry.guessedFrom =
            GuessSource{.file = readString(*guessed, "file", "guessed_from").value_or(""),
                        .modified = readString(*guessed, "modified", "guessed_from").value_or("")};
    }
    entry.isUncertain = readString(table, "confidence", {}) == "low";
    entry.name = readString(table, "name", {}).value_or("");
    entry.description = readString(table, "description", {}).value_or("");
    entry.notes = readString(table, "notes", {}).value_or("");
    entry.object = readString(table, "object", {});
    entry.scale = readNumber(table, "scale", {});
    entry.heightOffset = readNumber(table, "height_offset", {});
    entry.draw = readName(table, "draw", {}, DrawKindNames);
    if (const toml::table* rotates = table.get_as<toml::table>("rotates")) {
        entry.rotates = RotationAxes{.x = readBool(*rotates, "x", "rotates").value_or(true),
                                     .y = readBool(*rotates, "y", "rotates").value_or(true),
                                     .z = readBool(*rotates, "z", "rotates").value_or(true)};
    }
    if (const toml::table* fields = table.get_as<toml::table>("fields")) {
        entry.fields = readFields(*fields);
    }
    if (const toml::array* variants = table.get_as<toml::array>("variants")) {
        for (usize i = 0; i < variants->size(); ++i) {
            const std::string path = std::format("variants[{}]", i);
            if (const toml::table* variant = (*variants)[i].as_table()) {
                entry.variants.push_back(readVariant(*variant, path));
            } else {
                report((*variants)[i], path, "must be a table");
            }
        }
    }
    readEntryExtras(table, entry);
    return entry;
}

void DatabaseReader::readEntryExtras(const toml::table& table, ActorEntry& entry) {
    if (const toml::table* shadow = table.get_as<toml::table>("shadow")) {
        entry.shadow = readShadow(*shadow, "shadow");
    }
    if (const toml::table* spawn = table.get_as<toml::table>("spawn")) {
        entry.spawn = readSpawn(*spawn, "spawn");
    }
    entry.collision = readString(table, "collision", {});
    if (const toml::array* lights = table.get_as<toml::array>("lights")) {
        entry.lights = readLights(*lights, "lights");
    }
    if (const toml::array* ids = table.get_as<toml::array>("text_ids")) {
        entry.textIds = readTextIds(*ids, "text_ids");
    }
}

std::vector<ParamField> DatabaseReader::readFields(const toml::table& table) {
    std::vector<ParamField> fields;
    for (const auto& [name, node] : table) {
        const std::string path = makePath("fields", name.str());
        if (const toml::table* field = node.as_table()) {
            fields.push_back(readField(name.str(), *field, path));
        } else {
            report(node, path, "must be a table");
        }
    }
    return fields;
}

ParamField DatabaseReader::readField(const std::string_view name, const toml::table& table,
                                     const std::string_view path) {
    checkKeys(table, path, FieldKeys);
    ParamField field{
        .name = std::string(name),
        .location = {.file = file, .line = static_cast<u32>(table.source().begin.line)}};
    field.kind = readName(table, "kind", path, FieldKindNames).value_or(FieldKind::Integer);
    field.source = readName(table, "source", path, FieldSourceNames).value_or(FieldSource::Params);
    field.shift = static_cast<u8>(readInteger(table, "shift", path).value_or(0));
    field.width = static_cast<u8>(
        readInteger(table, "width", path).value_or(field.kind == FieldKind::Flag ? 1 : 16));
    field.isSigned = readBool(table, "signed", path).value_or(false);
    field.isHex = readBool(table, "hex", path).value_or(false);
    field.role = readName(table, "role", path, FieldRoleNames).value_or(FieldRole::None);
    field.meaning = readString(table, "meaning", path).value_or("");
    if (const toml::table* values = table.get_as<toml::table>("values")) {
        for (const auto& [valueName, value] : *values) {
            if (value.is_integer()) {
                field.values.push_back(EnumValue{.name = std::string(valueName.str()),
                                                 .value = value.value<s64>().value_or(0)});
            } else {
                report(value, makePath(makePath(path, "values"), valueName.str()),
                       "must be a whole number");
            }
        }
    }
    return field;
}

ActorVariant DatabaseReader::readVariant(const toml::table& table, const std::string_view path) {
    checkKeys(table, path, VariantKeys);
    ActorVariant variant{
        .location = {.file = file, .line = static_cast<u32>(table.source().begin.line)}};
    if (const toml::table* match = table.get_as<toml::table>("match")) {
        variant.match = readMatch(*match, makePath(path, "match"));
    }
    variant.object = readString(table, "object", path);
    variant.scale = readNumber(table, "scale", path);
    variant.heightOffset = readNumber(table, "height_offset", path);
    variant.draw = readName(table, "draw", path, DrawKindNames);
    variant.skeleton = readString(table, "skeleton", path);
    variant.animation = readString(table, "animation", path);
    variant.animationObject = readString(table, "animation_object", path);
    if (const std::optional<s64> frame = readInteger(table, "frame", path)) {
        variant.frame = static_cast<u32>(std::max<s64>(*frame, 0));
    }
    if (const toml::array* lists = table.get_as<toml::array>("display_lists")) {
        variant.displayLists = readDisplayLists(*lists, makePath(path, "display_lists"));
    }
    if (const toml::table* segments = table.get_as<toml::table>("segments")) {
        variant.segments = readSegments(*segments, makePath(path, "segments"));
    }
    if (const toml::table* colors = table.get_as<toml::table>("colors")) {
        variant.colors = readColors(*colors, makePath(path, "colors"));
    }
    variant.isBillboard = readBool(table, "billboard", path);
    if (const toml::array* children = table.get_as<toml::array>("children")) {
        variant.children = readChildren(*children, makePath(path, "children"));
    }
    if (const toml::table* spawn = table.get_as<toml::table>("spawn")) {
        variant.spawn = readSpawn(*spawn, makePath(path, "spawn"));
    }
    if (const std::optional<s64> params = readInteger(table, "default_params", path)) {
        variant.defaultParams = static_cast<u16>(*params);
    }
    if (const toml::array* steps = table.get_as<toml::array>("steps")) {
        variant.steps = readSteps(*steps, makePath(path, "steps"), false);
    }
    return variant;
}

std::vector<FieldMatch> DatabaseReader::readMatch(const toml::table& table,
                                                  const std::string_view path) {
    const auto readValue = [](const toml::node& node) -> std::optional<MatchValue> {
        if (node.is_boolean()) {
            return node.value<bool>().value_or(false);
        }
        if (node.is_integer()) {
            return node.value<s64>().value_or(0);
        }
        if (node.is_string()) {
            return node.value<std::string>().value_or("");
        }
        return std::nullopt;
    };
    std::vector<FieldMatch> match;
    for (const auto& [name, node] : table) {
        FieldMatch field{.field = std::string(name.str())};
        if (const toml::array* values = node.as_array()) {
            for (const toml::node& value : *values) {
                if (std::optional<MatchValue> read = readValue(value)) {
                    field.values.push_back(std::move(*read));
                }
            }
        } else if (std::optional<MatchValue> read = readValue(node)) {
            field.values.push_back(std::move(*read));
        }
        if (field.values.empty()) {
            report(node, makePath(path, name.str()),
                   "must be true, false, a number or a name, or a list of them");
            continue;
        }
        match.push_back(std::move(field));
    }
    return match;
}

std::vector<DisplayListPart> DatabaseReader::readDisplayLists(const toml::array& lists,
                                                              const std::string_view path) {
    std::vector<DisplayListPart> parts;
    for (usize i = 0; i < lists.size(); ++i) {
        const std::string at = std::format("{}[{}]", path, i);
        const toml::table* table = lists[i].as_table();
        const std::optional<std::string> name =
            table != nullptr ? readString(*table, "name", at) : std::nullopt;
        if (!name) {
            report(lists[i], at, "must be a table with the display list's name");
            continue;
        }
        checkKeys(*table, at, ListKeys);
        DisplayListPart part{.name = *name, .object = readString(*table, "object", at)};
        part.pass = readName(*table, "pass", at, PassNames).value_or(Pass::Opaque);
        part.placement = readPlacement(*table, at);
        if (const toml::table* colors = table->get_as<toml::table>("colors")) {
            part.colors = readColors(*colors, makePath(at, "colors"));
        }
        part.isBillboard = readBool(*table, "billboard", at);
        if (const toml::table* segments = table->get_as<toml::table>("segments")) {
            part.segments = readSegments(*segments, makePath(at, "segments"));
        }
        parts.push_back(std::move(part));
    }
    return parts;
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

DrawColors DatabaseReader::readColors(const toml::table& table, const std::string_view path) {
    DrawColors colors;
    if (const toml::node* primitive = table.get("prim")) {
        colors.primitive = readColor<4>(*primitive, makePath(path, "prim"));
    }
    if (const toml::node* environment = table.get("env")) {
        colors.environment = readColor<4>(*environment, makePath(path, "env"));
    }
    return colors;
}

std::vector<ChildActor> DatabaseReader::readChildren(const toml::array& children,
                                                     const std::string_view path) {
    std::vector<ChildActor> read;
    for (usize i = 0; i < children.size(); ++i) {
        const std::string at = std::format("{}[{}]", path, i);
        const toml::table* table = children[i].as_table();
        const std::optional<std::string> child =
            table != nullptr ? readString(*table, "actor", at) : std::nullopt;
        if (!child) {
            report(children[i], at, "must be a table with the child's actor");
            continue;
        }
        read.push_back(ChildActor{
            .actor = *child,
            .params = static_cast<u16>(readInteger(*table, "params", at).value_or(0)),
            .offset = readVector<3>(*table, "offset", at).value_or(std::array<f32, 3>{})});
    }
    return read;
}

std::optional<SpawnCondition> DatabaseReader::readSpawn(const toml::table& table,
                                                        const std::string_view path) {
    checkKeys(table, path, SpawnKeys);
    SpawnCondition spawn{.age = readName(table, "age", path, AgeNames),
                         .time = readName(table, "time", path, TimeNames)};
    if (const toml::table* flag = table.get_as<toml::table>("flag")) {
        const std::string at = makePath(path, "flag");
        spawn.flagField = readString(*flag, "field", at);
        spawn.isFlagSet = readString(*flag, "is", at).value_or("set") != "unset";
    }
    return spawn;
}

std::optional<Shadow> DatabaseReader::readShadow(const toml::table& table,
                                                 const std::string_view path) {
    const s64 alpha = readInteger(table, "alpha", path).value_or(0xFF);
    if (alpha < 0 || alpha > 0xFF) {
        report(*table.get("alpha"), makePath(path, "alpha"), "must be from 0 to 255");
    }
    return Shadow{.kind = readName(table, "kind", path, ShadowKindNames).value_or(ShadowKind::None),
                  .size = readNumber(table, "size", path).value_or(0.0F),
                  .alpha = static_cast<u8>(std::clamp<s64>(alpha, 0, 0xFF))};
}

std::vector<PointLight> DatabaseReader::readLights(const toml::array& lights,
                                                   const std::string_view path) {
    std::vector<PointLight> read;
    for (usize i = 0; i < lights.size(); ++i) {
        const std::string at = std::format("{}[{}]", path, i);
        const toml::table* table = lights[i].as_table();
        const toml::node* color = table != nullptr ? table->get("color") : nullptr;
        const std::optional<std::array<u8, 3>> rgb =
            color != nullptr ? readColor<3>(*color, makePath(at, "color")) : std::nullopt;
        if (!rgb) {
            if (color == nullptr) {
                report(lights[i], at, "must be a table with the light's color");
            }
            continue;
        }
        read.push_back(PointLight{
            .color = *rgb,
            .radius = readNumber(*table, "radius", at).value_or(0.0F),
            .offset = readVector<3>(*table, "offset", at).value_or(std::array<f32, 3>{})});
    }
    return read;
}

std::vector<u16> DatabaseReader::readTextIds(const toml::array& ids, const std::string_view path) {
    std::vector<u16> read;
    for (usize i = 0; i < ids.size(); ++i) {
        if (ids[i].is_integer()) {
            read.push_back(static_cast<u16>(ids[i].value<s64>().value_or(0)));
        } else {
            report(ids[i], std::format("{}[{}]", path, i), "must be a whole number");
        }
    }
    return read;
}

} // namespace toki::actor
