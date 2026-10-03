#pragma once

#include "app/core/Types.h"
#include "app/core/domain/Log.h"

#include <expected>
#include <filesystem>
#include <functional>
#include <map>
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

    /// Make's command line for the checkout, such as `VERSION` and
    /// `DEBUG_FEATURES`. Empty means the Makefile's own defaults. The workspace
    /// reads the checkout with these and the build runner builds with them, so
    /// both always agree on what the build is.
    std::map<std::string, std::string, std::less<>> build;

    std::optional<WindowGeometry> window;
    LogSettings log;
    std::optional<std::vector<std::string>> openPanels;

    /// Kept beside `openPanels` so a panel in neither list is one this config
    /// has never seen, which opens as its default says rather than closed.
    std::vector<std::string> closedPanels;

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
