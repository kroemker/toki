#include "PathDialog.h"

#include "app/core/domain/Log.h"
#include "app/core/domain/Utf8.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace toki::core {

void PathDialog::openFolder(SDL_Window* parent, const std::filesystem::path& startIn) {
    if (!begin()) {
        return;
    }
    const std::string start = encodeUtf8(startIn);
    SDL_ShowOpenFolderDialog(&PathDialog::handleChosen, this, parent,
                             start.empty() ? nullptr : start.c_str(), false);
}

void PathDialog::openProgram(SDL_Window* parent, const std::filesystem::path& startIn) {
    if (!begin()) {
        return;
    }
#ifdef _WIN32
    static constexpr SDL_DialogFileFilter Programs{.name = "Programs", .pattern = "exe"};
    const SDL_DialogFileFilter* filters = &Programs;
    const int filterCount = 1;
#else
    const SDL_DialogFileFilter* filters = nullptr;
    const int filterCount = 0;
#endif
    const std::string start = encodeUtf8(startIn);
    SDL_ShowOpenFileDialog(&PathDialog::handleChosen, this, parent, filters, filterCount,
                           start.empty() ? nullptr : start.c_str(), false);
}

bool PathDialog::begin() {
    const std::scoped_lock lock(mutex);
    if (pending) {
        return false;
    }
    pending = true;
    choice.reset();
    return true;
}

bool PathDialog::isPending() const {
    const std::scoped_lock lock(mutex);
    return pending;
}

std::optional<std::filesystem::path> PathDialog::takeChoice() {
    const std::scoped_lock lock(mutex);
    return std::exchange(choice, std::nullopt);
}

void PathDialog::handleChosen(void* userdata, const char* const* files, int /*filter*/) {
    auto* pathDialog = static_cast<PathDialog*>(userdata);

    if (files == nullptr) {
        logError("the file dialog failed: {}", SDL_GetError());
    } else if (files[0] != nullptr) {
        const std::scoped_lock lock(pathDialog->mutex);
        pathDialog->choice = decodeUtf8(files[0]);
        pathDialog->pending = false;
        return;
    }

    const std::scoped_lock lock(pathDialog->mutex);
    pathDialog->pending = false;
}

} // namespace toki::core
