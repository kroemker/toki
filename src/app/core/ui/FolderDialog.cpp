#include "FolderDialog.h"

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

void FolderDialog::open(SDL_Window* parent, const std::filesystem::path& startIn) {
    {
        const std::scoped_lock lock(mutex);
        if (pending) {
            return;
        }
        pending = true;
        choice.reset();
    }

    const std::string start = encodeUtf8(startIn);
    SDL_ShowOpenFolderDialog(&FolderDialog::handleChosen, this, parent,
                             start.empty() ? nullptr : start.c_str(), false);
}

bool FolderDialog::isPending() const {
    const std::scoped_lock lock(mutex);
    return pending;
}

std::optional<std::filesystem::path> FolderDialog::takeChoice() {
    const std::scoped_lock lock(mutex);
    return std::exchange(choice, std::nullopt);
}

void FolderDialog::handleChosen(void* userdata, const char* const* files, int /*filter*/) {
    auto* folderDialog = static_cast<FolderDialog*>(userdata);

    if (files == nullptr) {
        logError("folder folderDialog failed: {}", SDL_GetError());
    } else if (files[0] != nullptr) {
        const std::scoped_lock lock(folderDialog->mutex);
        folderDialog->choice = decodeUtf8(files[0]);
        folderDialog->pending = false;
        return;
    }

    const std::scoped_lock lock(folderDialog->mutex);
    folderDialog->pending = false;
}

} // namespace toki::core
