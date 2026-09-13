#pragma once

#include "app/core/Types.h"
#include "app/core/domain/Log.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace toki::core {

struct WindowGeometry {
    s32 x = 0;
    s32 y = 0;
    s32 width = 0;
    s32 height = 0;
    bool maximized = false;
};

struct LogSettings {
    LogLevel minLevel = LogLevel::Debug;
    usize maxEntries = Log::DefaultMaxEntries;
};

struct Settings {
    std::optional<std::filesystem::path> hackerootPath;
    std::optional<WindowGeometry> window;
    LogSettings log;
    std::optional<std::vector<std::string>> openPanels;

    [[nodiscard]] bool isConfigured() const {
        return hackerootPath.has_value();
    }
};

/// A missing file is not a failure: it yields defaults, and `isConfigured()`
/// is what tells the caller there is still a setup wizard to run.
[[nodiscard]] std::expected<Settings, std::string> loadSettings(const std::filesystem::path& path);

/// Rewrites only the keys this build knows about, so a config written by a
/// newer build keeps the fields this one cannot represent.
[[nodiscard]] std::expected<void, std::string> saveSettings(const Settings& settings,
                                                            const std::filesystem::path& path);

} // namespace toki::core
