#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace toki::core {

[[nodiscard]] std::expected<std::filesystem::path, std::string> configDir();

[[nodiscard]] std::expected<std::filesystem::path, std::string> assetDir();

} // namespace toki::core
