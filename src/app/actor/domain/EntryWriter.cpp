#include "ActorDatabaseFile.h"
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
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

[[nodiscard]] double makeNumber(const f32 value) {
    const std::string text = std::format("{}", value);
    double number = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), number);
    return number;
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

[[nodiscard]] toml::array makeChannels(const std::array<u8, 4>& channels) {
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

void insertPlacement(toml::table& table, const PartPlacement& placement) {
    if (!isZero(placement.translation)) {
        table.insert("translation", makeNumbers(placement.translation));
    }
    if (!isZero(placement.rotation)) {
        table.insert("rotation", makeNumbers(placement.rotation));
    }
    const std::array<f32, 3>& scale = placement.scale;
    if (scale[0] == scale[1] && scale[1] == scale[2]) {
        if (scale[0] != 1.0F) {
            table.insert("scale", makeNumber(scale[0]));
        }
    } else {
        table.insert("scale", makeNumbers(scale));
    }
}

[[nodiscard]] toml::table makeParam(const ActorParam& param) {
    toml::table table = makeInline();
    table.insert("mask", makeHex(param.mask));
    table.insert("kind", std::string(getEntryName(ParamKindNames, param.kind)));
    if (param.source != ParamSource::Params) {
        table.insert("source", std::string(getEntryName(ParamSourceNames, param.source)));
    }
    if (param.isSigned) {
        table.insert("signed", true);
    }
    if (param.isHex) {
        table.insert("hex", true);
    }
    if (param.collection) {
        table.insert("values", *param.collection);
    } else if (!param.values.empty()) {
        toml::table values = makeInline();
        for (const ChoiceValue& value : param.values) {
            values.insert(std::format("{}", value.value), value.label);
        }
        table.insert("values", std::move(values));
    }
    if (param.offset != 0) {
        table.insert("offset", makeHex(param.offset));
    }
    insertText(table, "description", param.description);
    return table;
}

[[nodiscard]] toml::table makeMatch(const std::vector<ParamMatch>& match) {
    toml::table table = makeInline();
    for (const ParamMatch& param : match) {
        if (param.values.size() == 1) {
            std::visit([&](const auto& value) { table.insert(param.param, value); },
                       param.values.front());
            continue;
        }
        toml::array values;
        for (const MatchValue& value : param.values) {
            std::visit([&values](const auto& candidate) { values.push_back(candidate); }, value);
        }
        table.insert(param.param, std::move(values));
    }
    return table;
}

[[nodiscard]] toml::table makePart(const ActorPart& part) {
    toml::table table = makeInline();
    table.insert(part.kind == PartKind::DisplayList ? "display_list" : "skeleton", part.symbol);
    insertOptional(table, "object", part.object);
    insertOptional(table, "pose", part.pose);
    insertOptional(table, "pose_object", part.poseObject);
    insertPlacement(table, part.placement);
    if (part.isBillboard) {
        table.insert("billboard", true);
    }
    if (part.colors.primitive) {
        table.insert("prim", makeChannels(*part.colors.primitive));
    }
    if (part.colors.environment) {
        table.insert("env", makeChannels(*part.colors.environment));
    }
    if (!part.segments.empty()) {
        toml::table segments = makeInline();
        for (const SegmentSymbol& segment : part.segments) {
            segments.insert(std::to_string(segment.segment), segment.name);
        }
        table.insert("segments", std::move(segments));
    }
    if (part.setup) {
        table.insert("setup", s64{*part.setup});
    }
    if (part.pass != Pass::Opaque) {
        table.insert("pass", std::string(getEntryName(PassNames, part.pass)));
    }
    if (part.isHilit) {
        table.insert("hilite", true);
    }
    return table;
}

[[nodiscard]] toml::table makeVariant(const ActorVariant& variant) {
    toml::table table;
    if (!variant.match.empty()) {
        table.insert("match", makeMatch(variant.match));
    }
    insertOptional(table, "object", variant.object);
    insertOptional(table, "scale", variant.scale);
    insertOptional(table, "height_offset", variant.heightOffset);
    if (variant.draw) {
        table.insert("draw", std::string(getEntryName(DrawKindNames, *variant.draw)));
    }
    if (!variant.parts.empty()) {
        toml::array parts;
        for (const ActorPart& part : variant.parts) {
            parts.push_back(makePart(part));
        }
        table.insert("parts", std::move(parts));
    }
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
    if (variant.defaultParams) {
        table.insert("default_params", makeHex(*variant.defaultParams));
    }
    return table;
}

[[nodiscard]] toml::table makeEntry(const ActorEntry& entry) {
    toml::table table;
    insertText(table, "name", entry.name);
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
    if (!entry.params.empty()) {
        toml::table params;
        for (const ActorParam& param : entry.params) {
            params.insert(param.name, makeParam(param));
        }
        table.insert("params", std::move(params));
    }
    if (!entry.variants.empty()) {
        toml::array variants;
        for (const ActorVariant& variant : entry.variants) {
            variants.push_back(makeVariant(variant));
        }
        table.insert("variants", std::move(variants));
    }
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
