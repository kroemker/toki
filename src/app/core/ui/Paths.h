#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace toki::core {

[[nodiscard]] std::expected<std::filesystem::path, std::string> findConfigDirectory();

[[nodiscard]] std::expected<std::filesystem::path, std::string> findAssetDirectory();

} // namespace toki::core
