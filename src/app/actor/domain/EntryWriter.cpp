#include "ActorDatabaseFile.h"
#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] double makeNumber(const f32 value) {
    return std::stod(std::format("{}", value));
}

[[nodiscard]] toml::value<s64> makeHex(const s64 value) {
    toml::value<s64> hex(value);
    hex.flags(toml::value_flags::format_as_hexadecimal);
    return hex;
}

[[nodiscard]] toml::table makeInline() {
    toml::table table;
    table.is_inline(true);
    return table;
}

template <usize Count>
[[nodiscard]] toml::array makeNumbers(const std::array<f32, Count>& values) {
    toml::array array;
    for (const f32 value : values) {
        array.push_back(makeNumber(value));
    }
    return array;
}

template <usize Count>
[[nodiscard]] toml::array makeChannels(const std::array<u8, Count>& channels) {
    toml::array array;
    for (const u8 channel : channels) {
        array.push_back(s64{channel});
    }
    return array;
}

[[nodiscard]] bool isZero(const std::array<f32, 3>& values) {
    return std::ranges::all_of(values, [](const f32 value) { return value == 0.0F; });
}

void insertText(toml::table& table, const std::string_view key, const std::string& text) {
    if (!text.empty()) {
        table.insert(key, text);
    }
}

void insertOptional(toml::table& table, const std::string_view key,
                    const std::optional<std::string>& text) {
    if (text) {
        table.insert(key, *text);
    }
}

void insertOptional(toml::table& table, const std::string_view key,
                    const std::optional<f32>& value) {
    if (value) {
        table.insert(key, makeNumber(*value));
    }
}

[[nodiscard]] toml::table makeColors(const DrawColors& colors) {
    toml::table table = makeInline();
    if (colors.primitive) {
        table.insert("prim", makeChannels(*colors.primitive));
    }
    if (colors.environment) {
        table.insert("env", makeChannels(*colors.environment));
    }
    return table;
}

[[nodiscard]] toml::table makeSegments(const std::vector<SegmentSymbol>& segments) {
    toml::table table = makeInline();
    for (const SegmentSymbol& segment : segments) {
        table.insert(std::to_string(segment.segment), segment.name);
    }
    return table;
}

[[nodiscard]] toml::table makeSpawn(const SpawnCondition& spawn) {
    toml::table table = makeInline();
    if (spawn.age) {
        table.insert("age", std::string(getEntryName(AgeNames, *spawn.age)));
    }
    if (spawn.time) {
        table.insert("time", std::string(getEntryName(TimeNames, *spawn.time)));
    }
    if (spawn.flagField) {
        toml::table flag = makeInline();
        flag.insert("field", *spawn.flagField);
        flag.insert("is", spawn.isFlagSet ? "set" : "unset");
        table.insert("flag", std::move(flag));
    }
    return table;
}

[[nodiscard]] toml::table makeField(const ParamField& field) {
    toml::table table = makeInline();
    table.insert("kind", std::string(getEntryName(FieldKindNames, field.kind)));
    if (field.source != FieldSource::Params) {
        table.insert("source", std::string(getEntryName(FieldSourceNames, field.source)));
    }
    table.insert("shift", s64{field.shift});
    if (field.kind != FieldKind::Flag) {
        table.insert("width", s64{field.width});
    }
    if (field.isSigned) {
        table.insert("signed", true);
    }
    if (field.isHex) {
        table.insert("hex", true);
    }
    if (!field.values.empty()) {
        toml::table values = makeInline();
        for (const EnumValue& value : field.values) {
            values.insert(value.name, value.value);
        }
        table.insert("values", std::move(values));
    }
    if (field.role != FieldRole::None) {
        table.insert("role", std::string(getEntryName(FieldRoleNames, field.role)));
    }
    insertText(table, "meaning", field.meaning);
    return table;
}

[[nodiscard]] toml::table makeDisplayList(const DisplayListPart& part) {
    toml::table table = makeInline();
    table.insert("name", part.name);
    insertOptional(table, "object", part.object);
    if (part.pass != Pass::Opaque) {
        table.insert("pass", std::string(getEntryName(PassNames, part.pass)));
    }
    if (!isZero(part.placement.translation)) {
        table.insert("translation", makeNumbers(part.placement.translation));
    }
    if (!isZero(part.placement.rotation)) {
        table.insert("rotation", makeNumbers(part.placement.rotation));
    }
    const std::array<f32, 3>& scale = part.placement.scale;
    if (scale[0] == scale[1] && scale[1] == scale[2]) {
        if (scale[0] != 1.0F) {
            table.insert("scale", makeNumber(scale[0]));
        }
    } else {
        table.insert("scale", makeNumbers(scale));
    }
    if (part.colors.primitive || part.colors.environment) {
        table.insert("colors", makeColors(part.colors));
    }
    if (part.isBillboard) {
        table.insert("billboard", *part.isBillboard);
    }
    if (!part.segments.empty()) {
        table.insert("segments", makeSegments(part.segments));
    }
    return table;
}

[[nodiscard]] toml::table makeMatch(const std::vector<FieldMatch>& match) {
    toml::table table = makeInline();
    for (const FieldMatch& field : match) {
        std::visit([&](const auto& value) { table.insert(field.field, value); }, field.value);
    }
    return table;
}

void insertDrawing(toml::table& table, const ActorVariant& variant) {
    if (variant.draw) {
        table.insert("draw", std::string(getEntryName(DrawKindNames, *variant.draw)));
    }
    insertOptional(table, "skeleton", variant.skeleton);
    insertOptional(table, "animation", variant.animation);
    insertOptional(table, "animation_object", variant.animationObject);
    if (variant.frame) {
        table.insert("frame", s64{*variant.frame});
    }
    if (!variant.displayLists.empty()) {
        toml::array lists;
        for (const DisplayListPart& part : variant.displayLists) {
            lists.push_back(makeDisplayList(part));
        }
        table.insert("display_lists", std::move(lists));
    }
    if (!variant.segments.empty()) {
        table.insert("segments", makeSegments(variant.segments));
    }
    if (variant.colors.primitive || variant.colors.environment) {
        table.insert("colors", makeColors(variant.colors));
    }
    if (variant.isBillboard) {
        table.insert("billboard", *variant.isBillboard);
    }
}

[[nodiscard]] toml::table makeVariant(const ActorVariant& variant) {
    toml::table table;
    if (!variant.match.empty()) {
        table.insert("match", makeMatch(variant.match));
    }
    insertOptional(table, "object", variant.object);
    insertOptional(table, "scale", variant.scale);
    insertOptional(table, "height_offset", variant.heightOffset);
    insertDrawing(table, variant);
    if (!variant.children.empty()) {
        toml::array children;
        for (const ChildActor& child : variant.children) {
            toml::table entry = makeInline();
            entry.insert("actor", child.actor);
            entry.insert("params", makeHex(child.params));
            entry.insert("offset", makeNumbers(child.offset));
            children.push_back(std::move(entry));
        }
        table.insert("children", std::move(children));
    }
    if (variant.spawn) {
        table.insert("spawn", makeSpawn(*variant.spawn));
    }
    if (variant.defaultParams) {
        table.insert("default_params", makeHex(*variant.defaultParams));
    }
    return table;
}

void insertExtras(toml::table& table, const ActorEntry& entry) {
    if (entry.shadow) {
        toml::table shadow = makeInline();
        shadow.insert("kind", std::string(getEntryName(ShadowKindNames, entry.shadow->kind)));
        shadow.insert("size", makeNumber(entry.shadow->size));
        table.insert("shadow", std::move(shadow));
    }
    if (entry.spawn) {
        table.insert("spawn", makeSpawn(*entry.spawn));
    }
    insertOptional(table, "collision", entry.collision);
    if (!entry.lights.empty()) {
        toml::array lights;
        for (const PointLight& light : entry.lights) {
            toml::table read = makeInline();
            read.insert("color", makeChannels(light.color));
            read.insert("radius", makeNumber(light.radius));
            read.insert("offset", makeNumbers(light.offset));
            lights.push_back(std::move(read));
        }
        table.insert("lights", std::move(lights));
    }
    if (!entry.textIds.empty()) {
        toml::array ids;
        for (const u16 id : entry.textIds) {
            ids.push_back(makeHex(id));
        }
        table.insert("text_ids", std::move(ids));
    }
}

void insertHeader(toml::table& table, const ActorEntry& entry) {
    table.insert("status", std::string(getEntryName(StatusNames, entry.status)));
    if (entry.guessedFrom) {
        toml::table guessed = makeInline();
        guessed.insert("file", entry.guessedFrom->file);
        guessed.insert("modified", entry.guessedFrom->modified);
        table.insert("guessed_from", std::move(guessed));
    }
    if (entry.isUncertain) {
        table.insert("confidence", "low");
    }
    insertText(table, "name", entry.name);
    insertText(table, "description", entry.description);
    insertText(table, "notes", entry.notes);
    insertOptional(table, "object", entry.object);
    insertOptional(table, "scale", entry.scale);
    insertOptional(table, "height_offset", entry.heightOffset);
    if (!entry.rotates.x || !entry.rotates.y || !entry.rotates.z) {
        toml::table rotates = makeInline();
        rotates.insert("x", entry.rotates.x);
        rotates.insert("y", entry.rotates.y);
        rotates.insert("z", entry.rotates.z);
        table.insert("rotates", std::move(rotates));
    }
    if (entry.draw) {
        table.insert("draw", std::string(getEntryName(DrawKindNames, *entry.draw)));
    }
}

[[nodiscard]] toml::table makeEntry(const ActorEntry& entry) {
    toml::table table;
    insertHeader(table, entry);
    if (!entry.fields.empty()) {
        toml::table fields;
        for (const ParamField& field : entry.fields) {
            fields.insert(field.name, makeField(field));
        }
        table.insert("fields", std::move(fields));
    }
    if (!entry.variants.empty()) {
        toml::array variants;
        for (const ActorVariant& variant : entry.variants) {
            variants.push_back(makeVariant(variant));
        }
        table.insert("variants", std::move(variants));
    }
    insertExtras(table, entry);
    return table;
}

} // namespace

std::string formatActorDatabase(const ActorDatabase& database) {
    toml::table root;
    root.insert("format", s64{DatabaseFormat});
    if (database.checkedAgainst) {
        root.insert("checked_against", *database.checkedAgainst);
    }
    toml::table actors;
    for (const auto& [name, entry] : database.entries) {
        actors.insert(name, makeEntry(entry));
    }
    root.insert("actors", std::move(actors));
    std::ostringstream text;
    text << toml::toml_formatter(root, toml::toml_formatter::default_flags &
                                           ~toml::format_flags::indentation)
         << '\n';
    return text.str();
}

} // namespace toki::actor
