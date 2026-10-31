#include "CollisionNames.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "base/Diagnostic.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

namespace {

constexpr s64 NamesFormat = 1;

struct SettingTable {
    SurfaceSetting setting;
    std::string_view key;
};

constexpr std::array<SettingTable, 6> Tables{{
    {SurfaceSetting::FloorType, "floor_type"},
    {SurfaceSetting::FloorProperty, "floor_property"},
    {SurfaceSetting::WallType, "wall_type"},
    {SurfaceSetting::Material, "material"},
    {SurfaceSetting::FloorEffect, "floor_effect"},
    {SurfaceSetting::ConveyorSpeed, "conveyor_speed"},
}};

[[nodiscard]] u32 findLine(const toml::node& node) {
    return static_cast<u32>(node.source().begin.line);
}

void readTable(CollisionNames& names, const toml::table& root, const SettingTable& table,
               const std::filesystem::path& file) {
    const toml::node* node = root.get(table.key);
    if (node == nullptr) {
        return;
    }
    const toml::array* entries = node->as_array();
    if (entries == nullptr) {
        names.diagnostics.push_back(
            base::makeError({.file = file, .line = findLine(*node)},
                            std::format("`{}` is not an array of tables", table.key)));
        return;
    }
    std::vector<SurfaceValueName>& values = names.values[table.setting];
    for (usize i = 0; i < entries->size(); ++i) {
        const toml::table* entry = entries->get(i)->as_table();
        const std::optional<s64> value =
            entry != nullptr ? (*entry)["value"].value<s64>() : std::nullopt;
        const std::optional<std::string> name =
            entry != nullptr ? (*entry)["name"].value<std::string>() : std::nullopt;
        if (!value || !name) {
            names.diagnostics.push_back(base::makeError(
                {.file = file, .line = findLine(*entries->get(i))},
                std::format("entry {} of `{}` needs a `value` and a `name`", i + 1, table.key)));
            continue;
        }
        values.push_back(SurfaceValueName{
            .value = *value,
            .constant = (*entry)["constant"].value_or(std::string()),
            .name = *name,
            .tooltip = (*entry)["tooltip"].value_or(std::string()),
        });
    }
    std::ranges::sort(values, {}, &SurfaceValueName::value);
}

[[nodiscard]] s64 getSettingValue(const SurfaceFields& fields, const SurfaceSetting setting) {
    switch (setting) {
    case SurfaceSetting::FloorType:
        return fields.floorType;
    case SurfaceSetting::FloorProperty:
        return fields.floorProperty;
    case SurfaceSetting::WallType:
        return fields.wallType;
    case SurfaceSetting::Material:
        return fields.material;
    case SurfaceSetting::FloorEffect:
        return fields.floorEffect;
    case SurfaceSetting::ConveyorSpeed:
        return fields.conveyorSpeed;
    }
    return 0;
}

} // namespace

std::string makeSurfaceValueLabel(const CollisionNames& names, const SurfaceSetting setting,
                                  const SurfaceFields& fields) {
    const s64 value = getSettingValue(fields, setting);
    const SurfaceValueName* named = names.findName(setting, value);
    return named != nullptr ? named->name : std::format("{}", value);
}

std::string makeSurfaceTypeLabel(const CollisionNames* names, const Collision& collision,
                                 const usize type) {
    if (type >= collision.surfaceTypes.size()) {
        return std::format("{} (past the end of the list)", type);
    }
    if (names == nullptr) {
        return std::format("{}", type);
    }
    return std::format("{} · {}", type,
                       makeSurfaceSummary(*names, readSurfaceFields(collision.surfaceTypes[type])));
}

std::string makeSurfaceSummary(const CollisionNames& names, const SurfaceFields& fields) {
    std::string summary = makeSurfaceValueLabel(names, SurfaceSetting::Material, fields);
    for (const SettingTable& table : Tables) {
        if (table.setting != SurfaceSetting::Material &&
            getSettingValue(fields, table.setting) != 0) {
            summary += " · " + makeSurfaceValueLabel(names, table.setting, fields);
        }
    }
    if (fields.exitIndex != 0) {
        summary += std::format(" · Exit {}", fields.exitIndex - 1);
    }
    if (fields.canHookshot) {
        summary += " · Hookshotable";
    }
    return summary;
}

const SurfaceValueName* CollisionNames::findName(const SurfaceSetting setting,
                                                 const s64 value) const {
    const auto found = values.find(setting);
    if (found == values.end()) {
        return nullptr;
    }
    const auto named = std::ranges::find(found->second, value, &SurfaceValueName::value);
    return named != found->second.end() ? &*named : nullptr;
}

std::filesystem::path makeShippedCollisionNamesPath(const std::filesystem::path& assetDirectory) {
    return assetDirectory / "collision.toml";
}

CollisionNames parseCollisionNames(const std::string_view text, const std::filesystem::path& file) {
    toml::table root;
    try {
        root = toml::parse(text, file.string());
    } catch (const toml::parse_error& error) {
        CollisionNames failed;
        failed.diagnostics.push_back(
            base::makeError({.file = file, .line = static_cast<u32>(error.source().begin.line)},
                            std::format("cannot be read as TOML: {}", error.description())));
        return failed;
    }
    CollisionNames names;
    if (root["format"].value<s64>() != NamesFormat) {
        names.diagnostics.push_back(base::makeError(
            {.file = file},
            std::format("is not of format {}, which this build reads", NamesFormat)));
        return names;
    }
    for (const SettingTable& table : Tables) {
        readTable(names, root, table, file);
    }
    return names;
}

CollisionNames readCollisionNames(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        CollisionNames failed;
        failed.diagnostics.push_back(base::makeError({.file = file}, "cannot be opened"));
        return failed;
    }
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    return parseCollisionNames(text, file);
}

} // namespace toki::scene
