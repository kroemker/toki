#include "Paths.h"

#include "app/core/domain/Utf8.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <expected>
#include <filesystem>
#include <string>

namespace toki::core {

namespace {

constexpr const char* PrefOrg = "";
constexpr const char* PrefApp = "toki";

} // namespace

std::expected<std::filesystem::path, std::string> configDir() {
    char* prefPath = SDL_GetPrefPath(PrefOrg, PrefApp);
    if (prefPath == nullptr) {
        return std::unexpected(std::string("cannot locate a config directory: ") + SDL_GetError());
    }

    std::filesystem::path path = fromUtf8(prefPath);
    SDL_free(prefPath);
    return path;
}

std::expected<std::filesystem::path, std::string> assetDir() {
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr) {
        return std::unexpected(std::string("cannot locate the executable directory: ") +
                               SDL_GetError());
    }
    return fromUtf8(basePath) / "assets";
}

} // namespace toki::core
