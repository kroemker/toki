#include "GameCode.h"

#include "hackeroot/AssetImage.h"
#include "hackeroot/Types.h"
#include "z64/MemoryImage.h"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace hackeroot {

namespace {

constexpr usize SetupDisplayListSize = usize{6} * 8;

} // namespace

SegmentFiles findGameCodeFiles(const std::filesystem::path& repository) {
    return SegmentFiles{.segment = 0,
                        .files = {repository / "src" / "code" / "z_rcp.c",
                                  repository / "src" / "code" / "z_scene_table.c"}};
}

SharedImageFiles makeSharedGameCode(const std::filesystem::path& repository) {
    SegmentFiles code = findGameCodeFiles(repository);
    std::vector<std::filesystem::path> watched = code.files;
    return SharedImageFiles{.files = {std::move(code)}, .watched = std::move(watched)};
}

GameSetup findGameSetup(const z64::MemoryImage& code) {
    GameSetup setup{.drawConfig = code.findAddress("sDefaultDisplayList")};
    const std::optional<z64::ImageSymbol> setups = code.findSymbol("sSetupDL");
    const std::optional<u32> start = code.findAddress("sSetupDL");
    if (setups && start && setups->size >= (SetupDisplayList25 + 1) * SetupDisplayListSize) {
        setup.setupDisplayList =
            *start + static_cast<u32>(SetupDisplayList25 * SetupDisplayListSize);
    }
    return setup;
}

} // namespace hackeroot
