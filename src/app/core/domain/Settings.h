#pragma once

#include "app/core/Types.h"
#include "app/core/domain/Log.h"
#include "app/viewport/domain/ViewportView.h"

#include <array>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

struct WindowGeometry {
    s32 x = 0;
    s32 y = 0;
    s32 width = 0;
    s32 height = 0;
    bool maximized = false;
};

/// What an actor the viewport does not draw as its model is marked with.
enum class MarkerShape : u8 {
    Cube,
    Diamond,
    Pin,
};

inline constexpr usize MarkerShapeCount = static_cast<usize>(MarkerShape::Pin) + 1;

/// What `shape` is called in the config.
[[nodiscard]] std::string_view getMarkerShapeName(MarkerShape shape);

/// The shape `name` calls, if any.
[[nodiscard]] std::optional<MarkerShape> parseMarkerShape(std::string_view name);

struct LogSettings {
    LogLevel minLevel = LogLevel::Debug;
    usize maxEntries = Log::DefaultMaxEntries;
};

/// Where an orbit camera stood: the point it circles, its angles in degrees,
/// and how far it is from the point.
struct CameraPose {
    std::array<f32, 3> target{};
    f32 yaw = 0.0F;
    f32 pitch = 0.0F;
    f32 distance = 0.0F;
};

/// The scene open when the app last closed, and how it was shown, to open it
/// again the same way.
struct SceneSession {
    std::filesystem::path directory;
    /// The game's scene layer, not a position in a list, so that it means the
    /// same layer however many the scene has when it is read again.
    usize layer = 0;
    usize lightSetting = 0;
    std::vector<usize> hiddenRooms;
    bool marksActors = true;
    bool drawsModels = true;
    std::optional<CameraPose> camera;
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

    std::optional<SceneSession> session;

    MarkerShape markerShape = MarkerShape::Cube;

    viewport::ViewportView viewport;

    /// The new actors of the checkout that the person chose not to derive, by
    /// enum name, so they are not asked about again until the set changes.
    std::vector<std::string> dismissedNewActors;

    [[nodiscard]] bool isConfigured() const {
        return hackerootPath.has_value();
    }
};

/// Whether two paths name the same file or folder however they were written:
/// with either separator, as the checkout's own paths and the ones read back
/// from the config are. Nothing on disk is asked.
[[nodiscard]] bool isSamePath(const std::filesystem::path& first,
                              const std::filesystem::path& second);

/// A missing file is not a failure: it yields defaults, and `isConfigured()`
/// is what tells the caller there is still a setup wizard to run.
[[nodiscard]] std::expected<Settings, std::string> loadSettings(const std::filesystem::path& path);

/// Rewrites only the keys this build knows about, so a config written by a
/// newer build keeps the fields this one cannot represent.
[[nodiscard]] std::expected<void, std::string> saveSettings(const Settings& settings,
                                                            const std::filesystem::path& path);

} // namespace toki::core
