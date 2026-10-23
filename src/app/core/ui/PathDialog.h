#pragma once

#include <filesystem>
#include <mutex>
#include <optional>

struct SDL_Window;

namespace toki::core {

/// The system's dialog for picking a folder or a file. It answers on another
/// thread, and the choice is taken on the UI thread once it has come.
class PathDialog {
public:
    PathDialog() = default;

    PathDialog(const PathDialog&) = delete;
    PathDialog& operator=(const PathDialog&) = delete;
    PathDialog(PathDialog&&) = delete;
    PathDialog& operator=(PathDialog&&) = delete;
    ~PathDialog() = default;

    void openFolder(SDL_Window* parent, const std::filesystem::path& startIn);

    /// Offers programs only where there is such a thing, which on Windows is
    /// an `.exe`.
    void openProgram(SDL_Window* parent, const std::filesystem::path& startIn);

    [[nodiscard]] bool isPending() const;

    [[nodiscard]] std::optional<std::filesystem::path> takeChoice();

private:
    [[nodiscard]] bool begin();
    static void handleChosen(void* userdata, const char* const* files, int filter);

    mutable std::mutex mutex;
    std::optional<std::filesystem::path> choice;
    bool pending = false;
};

} // namespace toki::core
