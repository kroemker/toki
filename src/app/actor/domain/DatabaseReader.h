#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "base/Diagnostic.h"

#include <toml++/toml.hpp>

#include <array>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::actor {

using Keys = std::span<const std::string_view>;

inline constexpr std::array DatabaseKeys = {
    std::string_view("format"), std::string_view("checked_against"), std::string_view("actors")};
inline constexpr std::array EntryKeys = {
    std::string_view("status"),        std::string_view("guessed_from"),
    std::string_view("confidence"),    std::string_view("name"),
    std::string_view("description"),   std::string_view("notes"),
    std::string_view("object"),        std::string_view("scale"),
    std::string_view("height_offset"), std::string_view("rotates"),
    std::string_view("draw"),          std::string_view("fields"),
    std::string_view("variants"),      std::string_view("shadow"),
    std::string_view("spawn"),         std::string_view("collision"),
    std::string_view("lights"),        std::string_view("text_ids")};
inline constexpr std::array VariantKeys = {
    std::string_view("match"),     std::string_view("object"),
    std::string_view("scale"),     std::string_view("height_offset"),
    std::string_view("draw"),      std::string_view("skeleton"),
    std::string_view("animation"), std::string_view("animation_object"),
    std::string_view("frame"),     std::string_view("display_lists"),
    std::string_view("segments"),  std::string_view("colors"),
    std::string_view("billboard"), std::string_view("children"),
    std::string_view("spawn"),     std::string_view("default_params"),
    std::string_view("steps")};
inline constexpr std::array FieldKeys = {
    std::string_view("kind"),   std::string_view("source"), std::string_view("shift"),
    std::string_view("width"),  std::string_view("signed"), std::string_view("hex"),
    std::string_view("values"), std::string_view("role"),   std::string_view("meaning")};
inline constexpr std::array ListKeys = {
    std::string_view("name"),      std::string_view("pass"),    std::string_view("translation"),
    std::string_view("rotation"),  std::string_view("scale"),   std::string_view("colors"),
    std::string_view("billboard"), std::string_view("segments")};

/// `path.key`, or `key` at the top.
[[nodiscard]] std::string makePath(std::string_view path, std::string_view key);

/// Reads the tables of one `actors.toml` into an `ActorDatabase`, reporting
/// what it cannot read by file, line, actor and key. Internal to the actor
/// database's reading.
class DatabaseReader {
public:
    explicit DatabaseReader(std::filesystem::path file) : file(std::move(file)) {}

    [[nodiscard]] ActorDatabase read(const toml::table& root) &&;

private:
    void report(const toml::node& at, std::string_view key, std::string_view message) {
        const std::string where = actor.empty() ? std::string(key)
                                  : key.empty() ? actor
                                                : std::format("{}.{}", actor, key);
        database.diagnostics.push_back(
            base::makeWarning({.file = file, .line = static_cast<u32>(at.source().begin.line)},
                              std::format("{}: {}", where, message)));
    }

    void checkKeys(const toml::table& table, std::string_view key, Keys known);

    [[nodiscard]] std::optional<std::string>
    readString(const toml::table& table, std::string_view key, std::string_view path);
    [[nodiscard]] std::optional<f32> readNumber(const toml::table& table, std::string_view key,
                                                std::string_view path);
    [[nodiscard]] std::optional<s64> readInteger(const toml::table& table, std::string_view key,
                                                 std::string_view path);
    [[nodiscard]] std::optional<bool> readBool(const toml::table& table, std::string_view key,
                                               std::string_view path);
    template <typename Enum, usize Count>
    [[nodiscard]] std::optional<Enum> readName(const toml::table& table, std::string_view key,
                                               std::string_view path,
                                               const std::array<EntryName<Enum>, Count>& names);
    template <usize Count>
    [[nodiscard]] std::optional<std::array<f32, Count>>
    readVector(const toml::table& table, std::string_view key, std::string_view path);
    template <usize Count>
    [[nodiscard]] std::optional<std::array<u8, Count>> readColor(const toml::node& node,
                                                                 std::string_view path);

    [[nodiscard]] ActorEntry readEntry(std::string_view name, const toml::table& table);
    void readEntryExtras(const toml::table& table, ActorEntry& entry);
    [[nodiscard]] std::vector<ParamField> readFields(const toml::table& table);
    [[nodiscard]] ParamField readField(std::string_view name, const toml::table& table,
                                       std::string_view path);
    [[nodiscard]] ActorVariant readVariant(const toml::table& table, std::string_view path);
    [[nodiscard]] std::vector<FieldMatch> readMatch(const toml::table& table,
                                                    std::string_view path);
    [[nodiscard]] std::vector<DisplayListPart> readDisplayLists(const toml::array& lists,
                                                                std::string_view path);
    [[nodiscard]] std::vector<SegmentSymbol> readSegments(const toml::table& table,
                                                          std::string_view path);
    [[nodiscard]] DrawColors readColors(const toml::table& table, std::string_view path);
    [[nodiscard]] std::vector<ChildActor> readChildren(const toml::array& children,
                                                       std::string_view path);
    [[nodiscard]] std::optional<SpawnCondition> readSpawn(const toml::table& table,
                                                          std::string_view path);
    [[nodiscard]] std::vector<PointLight> readLights(const toml::array& lights,
                                                     std::string_view path);
    [[nodiscard]] PartPlacement readPlacement(const toml::table& table, std::string_view path);
    [[nodiscard]] std::optional<Shadow> readShadow(const toml::table& table, std::string_view path);
    [[nodiscard]] std::vector<u16> readTextIds(const toml::array& ids, std::string_view path);
    [[nodiscard]] std::vector<DrawStep> readSteps(const toml::array& steps, std::string_view path,
                                                  bool isInLimb);
    [[nodiscard]] std::optional<DrawStep> readStep(const toml::table& table, std::string_view path,
                                                   bool isInLimb);
    [[nodiscard]] std::optional<DrawStep> readSkeletonStep(const toml::table& table,
                                                           std::string_view path);
    [[nodiscard]] TransformStep readTransform(const toml::table& table, std::string_view path);

    std::filesystem::path file;
    std::string actor;
    ActorDatabase database;
};

template <typename Enum, usize Count>
std::optional<Enum> DatabaseReader::readName(const toml::table& table, const std::string_view key,
                                             const std::string_view path,
                                             const std::array<EntryName<Enum>, Count>& names) {
    const std::optional<std::string> name = readString(table, key, path);
    if (!name) {
        return std::nullopt;
    }
    if (const std::optional<Enum> value = findEntryValue(names, *name)) {
        return value;
    }
    std::string allowed;
    for (const EntryName<Enum>& entry : names) {
        allowed += allowed.empty() ? std::string(entry.name) : std::format(", {}", entry.name);
    }
    report(*table.get(key), makePath(path, key),
           std::format("'{}' is not one of {}", *name, allowed));
    return std::nullopt;
}

template <usize Count>
std::optional<std::array<f32, Count>> DatabaseReader::readVector(const toml::table& table,
                                                                 const std::string_view key,
                                                                 const std::string_view path) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::nullopt;
    }
    const toml::array* array = node->as_array();
    std::array<f32, Count> vector{};
    if (array == nullptr || array->size() != Count ||
        !std::ranges::all_of(*array,
                             [](const toml::node& element) { return element.is_number(); })) {
        report(*node, makePath(path, key), std::format("must be {} numbers", Count));
        return std::nullopt;
    }
    for (usize i = 0; i < Count; ++i) {
        vector[i] = static_cast<f32>((*array)[i].value<double>().value_or(0.0));
    }
    return vector;
}

template <usize Count>
std::optional<std::array<u8, Count>> DatabaseReader::readColor(const toml::node& node,
                                                               const std::string_view path) {
    const toml::array* array = node.as_array();
    std::array<u8, Count> color{};
    const bool takesAlpha = Count == 4;
    if (array != nullptr && (array->size() == Count || (takesAlpha && array->size() == 3))) {
        color.back() = 0xFF;
        bool isColor = true;
        for (usize i = 0; i < array->size(); ++i) {
            const std::optional<s64> channel = (*array)[i].value<s64>();
            isColor =
                isColor && (*array)[i].is_integer() && channel && *channel >= 0 && *channel <= 0xFF;
            color[i] = static_cast<u8>(channel.value_or(0));
        }
        if (isColor) {
            return color;
        }
    }
    report(node, path,
           takesAlpha ? "must be 3 or 4 whole numbers from 0 to 255"
                      : std::format("must be {} whole numbers from 0 to 255", Count));
    return std::nullopt;
}

} // namespace toki::actor
