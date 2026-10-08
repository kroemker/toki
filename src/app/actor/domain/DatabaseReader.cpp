#include "DatabaseReader.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "base/Diagnostic.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::actor {

std::string makePath(const std::string_view path, const std::string_view key) {
    return path.empty() ? std::string(key) : std::format("{}.{}", path, key);
}

void DatabaseReader::checkKeys(const toml::table& table, const std::string_view key,
                               const Keys known) {
    for (const auto& [name, node] : table) {
        if (!std::ranges::contains(known, name.str())) {
            report(node, makePath(key, name.str()),
                   "is not a key this build knows, so it is ignored");
        }
    }
}

std::optional<std::string> DatabaseReader::readString(const toml::table& table,
                                                      const std::string_view key,
                                                      const std::string_view path) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::nullopt;
    }
    if (std::optional<std::string> value = node->value<std::string>(); value && node->is_string()) {
        return value;
    }
    report(*node, makePath(path, key), "must be a string");
    return std::nullopt;
}

std::optional<f32> DatabaseReader::readNumber(const toml::table& table, const std::string_view key,
                                              const std::string_view path) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::nullopt;
    }
    if (node->is_number()) {
        return static_cast<f32>(node->value<double>().value_or(0.0));
    }
    report(*node, makePath(path, key), "must be a number");
    return std::nullopt;
}

std::optional<s64> DatabaseReader::readInteger(const toml::table& table, const std::string_view key,
                                               const std::string_view path) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::nullopt;
    }
    if (node->is_integer()) {
        return node->value<s64>();
    }
    report(*node, makePath(path, key), "must be a whole number");
    return std::nullopt;
}

std::optional<bool> DatabaseReader::readBool(const toml::table& table, const std::string_view key,
                                             const std::string_view path) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::nullopt;
    }
    if (node->is_boolean()) {
        return node->value<bool>();
    }
    report(*node, makePath(path, key), "must be true or false");
    return std::nullopt;
}

} // namespace toki::actor
