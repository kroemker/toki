#pragma once

#include <filesystem>
#include <mutex>
#include <optional>

struct SDL_Window;

namespace toki::core {

class FolderDialog {
public:
    FolderDialog() = default;

    FolderDialog(const FolderDialog&) = delete;
    FolderDialog& operator=(const FolderDialog&) = delete;
    FolderDialog(FolderDialog&&) = delete;
    FolderDialog& operator=(FolderDialog&&) = delete;
    ~FolderDialog() = default;

    void open(SDL_Window* parent, const std::filesystem::path& startIn);

    [[nodiscard]] bool isPending() const;

    [[nodiscard]] std::optional<std::filesystem::path> takeChoice();

private:
    static void handleChosen(void* userdata, const char* const* files, int filter);

    mutable std::mutex mutex;
    std::optional<std::filesystem::path> choice;
    bool pending = false;
};

} // namespace toki::core
