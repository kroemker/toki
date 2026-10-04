#include "SceneNames.h"

#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace toki::assets {

std::string makeReadableSceneName(std::string_view enumName) {
    constexpr std::string_view Prefix = "SCENE_";
    if (enumName.starts_with(Prefix)) {
        enumName.remove_prefix(Prefix.size());
    }
    std::string readable;
    bool startOfWord = true;
    for (const char c : enumName) {
        if (c == '_') {
            readable += ' ';
            startOfWord = true;
            continue;
        }
        const auto byte = static_cast<unsigned char>(c);
        readable += static_cast<char>(startOfWord ? std::toupper(byte) : std::tolower(byte));
        startOfWord = false;
    }
    return readable;
}

const hackeroot::SceneEntry* findSceneEntry(const hackeroot::Tables& tables,
                                            const std::string_view folderName) {
    const std::string tableName = std::string(folderName) + "_scene";
    const auto found = std::ranges::find(tables.scenes, tableName, &hackeroot::SceneEntry::name);
    return found == tables.scenes.end() ? nullptr : &*found;
}

std::string getAssetCategory(const hackeroot::AssetDirectory& asset) {
    std::string category = asset.directory.parent_path().filename().string();
    if (category == "scenes" || category == "objects") {
        return "other";
    }
    return category;
}

} // namespace toki::assets
