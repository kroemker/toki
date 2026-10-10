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
#include <system_error>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr s64 LargestMask = 0xFFFF;

[[nodiscard]] bool isParams(const s64 value) {
    return value >= 0 && value <= LargestMask;
}

[[nodiscard]] std::optional<s64> parseNumber(std::string_view text) {
    const bool isNegative = text.starts_with('-');
    if (isNegative) {
        text.remove_prefix(1);
    }
    const bool isHex = text.starts_with("0x") || text.starts_with("0X");
    if (isHex) {
        text.remove_prefix(2);
    }
    s64 value = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value, isHex ? 16 : 10);
    if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return isNegative ? -value : value;
}

[[nodiscard]] std::optional<MatchValue> readMatchValue(const toml::node& node) {
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
}

} // namespace

ActorDatabase DatabaseReader::read(const toml::table& root) && {
    checkKeys(root, {}, DatabaseKeys);
    if (const std::optional<s64> format = readInteger(root, "format", {});
        format && *format != DatabaseFormat) {
        report(*root.get("format"), "format",
               std::format("is {}, and this build reads format {}; a database of format 1 was "
                           "written by an earlier Toki, and is made again by deriving its "
                           "actors",
                           *format, DatabaseFormat));
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
    entry.name = readString(table, "name", {}).value_or("");
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
    if (const toml::table* params = table.get_as<toml::table>("params")) {
        entry.params = readParams(*params);
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
    return entry;
}

std::vector<ActorParam> DatabaseReader::readParams(const toml::table& table) {
    std::vector<ActorParam> params;
    for (const auto& [name, node] : table) {
        const std::string path = makePath("params", name.str());
        if (const toml::table* param = node.as_table()) {
            params.push_back(readParam(name.str(), *param, path));
        } else {
            report(node, path, "must be a table");
        }
    }
    std::ranges::stable_sort(params, {}, [](const ActorParam& param) {
        return std::pair(param.source, findMaskShift(param.mask));
    });
    return params;
}

ActorParam DatabaseReader::readParam(const std::string_view name, const toml::table& table,
                                     const std::string_view path) {
    checkKeys(table, path, ParamKeys);
    ActorParam param{
        .name = std::string(name),
        .location = {.file = file, .line = static_cast<u32>(table.source().begin.line)}};
    const std::optional<s64> mask = readInteger(table, "mask", path);
    if (mask && (*mask < 0 || *mask > LargestMask)) {
        report(table, makePath(path, "mask"), "must be a mask of up to 16 bits, as 0x0F00");
    }
    param.mask = static_cast<u16>(std::clamp<s64>(mask.value_or(0), 0, LargestMask));
    param.kind = readName(table, "kind", path, ParamKindNames).value_or(ParamKind::Number);
    param.source = readName(table, "source", path, ParamSourceNames).value_or(ParamSource::Params);
    param.isSigned = readBool(table, "signed", path).value_or(false);
    param.isHex = readBool(table, "hex", path).value_or(false);
    param.description = readString(table, "description", path).value_or("");
    if (const toml::table* values = table.get_as<toml::table>("values")) {
        param.values = readChoices(*values, makePath(path, "values"));
    }
    return param;
}

std::vector<ChoiceValue> DatabaseReader::readChoices(const toml::table& table,
                                                     const std::string_view path) {
    std::vector<ChoiceValue> choices;
    for (const auto& [key, node] : table) {
        const std::optional<s64> value = parseNumber(key.str());
        const std::optional<std::string> label = node.value<std::string>();
        if (!value || !node.is_string() || !label) {
            report(node, makePath(path, key.str()),
                   "must be a number set to the label shown for it");
            continue;
        }
        if (std::ranges::contains(choices, *value, &ChoiceValue::value)) {
            report(node, makePath(path, key.str()), std::format("labels {} again", *value));
            continue;
        }
        choices.push_back(ChoiceValue{.value = *value, .label = *label});
    }
    std::ranges::sort(choices, {}, &ChoiceValue::value);
    return choices;
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
    if (const toml::array* parts = table.get_as<toml::array>("parts")) {
        variant.parts = readParts(*parts, makePath(path, "parts"));
    }
    if (const toml::array* children = table.get_as<toml::array>("children")) {
        variant.children = readChildren(*children, makePath(path, "children"));
    }
    if (const std::optional<s64> params = readInteger(table, "default_params", path)) {
        if (isParams(*params)) {
            variant.defaultParams = static_cast<u16>(*params);
        } else {
            report(*table.get("default_params"), makePath(path, "default_params"),
                   "must be params of 16 bits, 0x0000 to 0xFFFF");
        }
    }
    return variant;
}

std::vector<ParamMatch> DatabaseReader::readMatch(const toml::table& table,
                                                  const std::string_view path) {
    std::vector<ParamMatch> match;
    for (const auto& [name, node] : table) {
        ParamMatch param{.param = std::string(name.str())};
        const std::string at = makePath(path, name.str());
        if (const toml::array* values = node.as_array()) {
            for (const toml::node& value : *values) {
                if (std::optional<MatchValue> read = readMatchValue(value)) {
                    param.values.push_back(std::move(*read));
                } else {
                    report(value, at,
                           "lists something that is not true, false, a number or a name");
                }
            }
        } else if (std::optional<MatchValue> read = readMatchValue(node)) {
            param.values.push_back(std::move(*read));
        } else {
            report(node, at, "must be true, false, a number or a name, or a list of them");
            continue;
        }
        match.push_back(std::move(param));
    }
    return match;
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
        const s64 params = readInteger(*table, "params", at).value_or(0);
        if (!isParams(params)) {
            report(children[i], makePath(at, "params"),
                   "must be params of 16 bits, 0x0000 to 0xFFFF");
            continue;
        }
        read.push_back(ChildActor{
            .actor = *child,
            .params = static_cast<u16>(params),
            .offset = readVector<3>(*table, "offset", at).value_or(std::array<f32, 3>{})});
    }
    return read;
}

} // namespace toki::actor
