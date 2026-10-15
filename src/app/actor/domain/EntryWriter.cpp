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
    if (param.sceneFlag) {
        table.insert("scene_flag", std::string(getEntryName(SceneFlagNames, *param.sceneFlag)));
        if (param.flagUse != FlagUse::Reads) {
            table.insert("flag_use", std::string(getEntryName(FlagUseNames, param.flagUse)));
        }
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

[[nodiscard]] toml::table makeLimbs(std::vector<LimbOverride> limbs) {
    std::ranges::sort(limbs, {}, &LimbOverride::limb);
    toml::table table = makeInline();
    for (const LimbOverride& limb : limbs) {
        const std::string key = std::to_string(limb.limb);
        if (!limb.displayList) {
            table.insert(key, false);
        } else if (limb.object) {
            toml::table other = makeInline();
            other.insert("display_list", *limb.displayList);
            other.insert("object", *limb.object);
            table.insert(key, std::move(other));
        } else {
            table.insert(key, *limb.displayList);
        }
    }
    return table;
}

[[nodiscard]] toml::table makeTileSize(const TileSize& size) {
    toml::table table = makeInline();
    table.insert("tile", s64{size.tile});
    if (size.x != 0) {
        table.insert("x", s64{size.x});
    }
    if (size.y != 0) {
        table.insert("y", s64{size.y});
    }
    table.insert("width", s64{size.width});
    table.insert("height", s64{size.height});
    return table;
}

[[nodiscard]] toml::table makeBuiltList(const BuiltList& built) {
    toml::table table = makeInline();
    if (built.colors.primitive) {
        table.insert("prim", makeChannels(*built.colors.primitive));
    }
    if (built.colors.environment) {
        table.insert("env", makeChannels(*built.colors.environment));
    }
    if (!built.renderMode.empty()) {
        toml::array cycles;
        for (const std::string& cycle : built.renderMode) {
            cycles.push_back(cycle);
        }
        table.insert("render_mode", std::move(cycles));
    }
    if (!built.tiles.empty()) {
        toml::array tiles;
        for (const TileSize& size : built.tiles) {
            tiles.push_back(makeTileSize(size));
        }
        table.insert("tiles", std::move(tiles));
    }
    return table;
}

[[nodiscard]] toml::table makeSegmentMatrices(const SegmentMatrices& target) {
    toml::table table = makeInline();
    toml::array matrices;
    for (const PartPlacement& placement : target.matrices) {
        toml::table matrix = makeInline();
        insertPlacement(matrix, placement);
        matrices.push_back(std::move(matrix));
    }
    table.insert("matrices", std::move(matrices));
    if (target.limb) {
        table.insert("limb", s64{*target.limb});
    }
    return table;
}

[[nodiscard]] toml::table makeSegmentCode(const SegmentCode& target) {
    toml::table table = makeInline();
    table.insert("code", target.symbol);
    if (target.index != 0) {
        table.insert("index", s64{target.index});
    }
    return table;
}

void insertSegments(toml::table& table, const std::vector<PartSegment>& segments) {
    if (segments.empty()) {
        return;
    }
    toml::table written = makeInline();
    for (const PartSegment& segment : segments) {
        const std::string key = std::to_string(segment.segment);
        if (const auto* symbol = std::get_if<std::string>(&segment.target)) {
            written.insert(key, *symbol);
        } else if (const auto* other = std::get_if<SegmentObject>(&segment.target)) {
            toml::table target = makeInline();
            target.insert("object", other->object);
            if (!other->symbol.empty()) {
                target.insert("symbol", other->symbol);
            }
            written.insert(key, std::move(target));
        } else if (const auto* matrices = std::get_if<SegmentMatrices>(&segment.target)) {
            written.insert(key, makeSegmentMatrices(*matrices));
        } else if (const auto* code = std::get_if<SegmentCode>(&segment.target)) {
            written.insert(key, makeSegmentCode(*code));
        } else {
            written.insert(key, makeBuiltList(std::get<BuiltList>(segment.target)));
        }
    }
    table.insert("segments", std::move(written));
}

void insertColors(toml::table& table, const DrawColors& colors) {
    if (colors.primitive) {
        table.insert("prim", makeChannels(*colors.primitive));
    }
    if (colors.environment) {
        table.insert("env", makeChannels(*colors.environment));
    }
}

[[nodiscard]] toml::table makeRotates(const RotationAxes& rotates) {
    toml::table table = makeInline();
    table.insert("x", rotates.x);
    table.insert("y", rotates.y);
    table.insert("z", rotates.z);
    return table;
}

[[nodiscard]] toml::table makePart(const ActorPart& part) {
    toml::table table = makeInline();
    if (part.kind == PartKind::Item) {
        if (part.itemChoice) {
            toml::table choice = makeInline();
            choice.insert("param", part.itemChoice->param);
            insertOptional(choice, "table", part.itemChoice->table);
            table.insert("get_item", std::move(choice));
        } else {
            table.insert("get_item", part.symbol);
        }
    } else {
        table.insert(part.kind == PartKind::DisplayList ? "display_list" : "skeleton", part.symbol);
    }
    insertOptional(table, "object", part.object);
    insertOptional(table, "pose", part.pose);
    insertOptional(table, "pose_object", part.poseObject);
    if (part.poseFrame != 0) {
        table.insert("pose_frame", s64{part.poseFrame});
    }
    if (!part.limbs.empty()) {
        table.insert("limbs", makeLimbs(part.limbs));
    }
    if (part.limb) {
        table.insert("limb", s64{*part.limb});
    }
    insertPlacement(table, part.placement);
    if (part.isBillboard) {
        table.insert("billboard", true);
    }
    insertColors(table, part.colors);
    insertSegments(table, part.segments);
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
            if (!child.takes.empty()) {
                toml::table takes = makeInline();
                for (const ParamTake& take : child.takes) {
                    takes.insert(take.to, take.from);
                }
                entry.insert("take", std::move(takes));
            }
            children.push_back(std::move(entry));
        }
        table.insert("children", std::move(children));
    }
    if (variant.defaultParams) {
        table.insert("default_params", makeHex(*variant.defaultParams));
    }
    return table;
}

[[nodiscard]] toml::table makeAdjustment(const ActorAdjustment& adjustment) {
    toml::table table;
    if (!adjustment.match.empty()) {
        table.insert("match", makeMatch(adjustment.match));
    }
    insertOptional(table, "scale", adjustment.scale);
    insertOptional(table, "height_offset", adjustment.heightOffset);
    if (adjustment.rotates) {
        table.insert("rotates", makeRotates(*adjustment.rotates));
    }
    insertColors(table, adjustment.colors);
    insertSegments(table, adjustment.segments);
    if (!adjustment.parts.empty()) {
        toml::array parts;
        for (const usize part : adjustment.parts) {
            parts.push_back(static_cast<s64>(part));
        }
        table.insert("parts", std::move(parts));
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
        table.insert("rotates", makeRotates(entry.rotates));
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
    if (!entry.adjustments.empty()) {
        toml::array adjustments;
        for (const ActorAdjustment& adjustment : entry.adjustments) {
            adjustments.push_back(makeAdjustment(adjustment));
        }
        table.insert("adjustments", std::move(adjustments));
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
