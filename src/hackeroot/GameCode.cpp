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

std::optional<u32> findSetupDisplayList(const z64::MemoryImage& code, const usize index) {
    const std::optional<z64::ImageSymbol> setups = code.findSymbol("sSetupDL");
    const std::optional<u32> start = code.findAddress("sSetupDL");
    if (!setups || !start || setups->size < (index + 1) * SetupDisplayListSize) {
        return std::nullopt;
    }
    return *start + static_cast<u32>(index * SetupDisplayListSize);
}

std::vector<u32> listSetupDisplayLists(const z64::MemoryImage& code) {
    const std::optional<z64::ImageSymbol> setups = code.findSymbol("sSetupDL");
    const std::optional<u32> start = code.findAddress("sSetupDL");
    std::vector<u32> lists;
    if (!setups || !start) {
        return lists;
    }
    for (usize index = 0; (index + 1) * SetupDisplayListSize <= setups->size; ++index) {
        lists.push_back(*start + static_cast<u32>(index * SetupDisplayListSize));
    }
    return lists;
}

GameSetup findGameSetup(const z64::MemoryImage& code) {
    return GameSetup{.drawConfig = code.findAddress("sDefaultDisplayList"),
                     .setupDisplayList = findSetupDisplayList(code, SetupDisplayList25)};
}

} // namespace hackeroot
