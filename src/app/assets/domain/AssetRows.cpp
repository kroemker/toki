#include "AssetRows.h"

#include "app/assets/Types.h"
#include "app/assets/domain/SceneNames.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <cctype>
#include <compare>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::assets {

namespace {

char makeLower(const char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool containsIgnoringCase(const std::string_view text, const std::string_view wanted) {
    return wanted.empty() || !std::ranges::search(text, wanted, [](const char a, const char b) {
                                  return makeLower(a) == makeLower(b);
                              }).empty();
}

std::strong_ordering compareNames(const std::string& a, const std::string& b) {
    return std::lexicographical_compare_three_way(
        a.begin(), a.end(), b.begin(), b.end(),
        [](const char x, const char y) { return makeLower(x) <=> makeLower(y); });
}

usize getIdKey(const hackeroot::SceneEntry* entry) {
    return entry != nullptr ? entry->id : static_cast<usize>(-1);
}

usize getIdKey(const hackeroot::ObjectEntry* entry) {
    return entry != nullptr ? entry->id : static_cast<usize>(-1);
}

std::strong_ordering compareScenes(const SceneRow& a, const SceneRow& b, const AssetColumn column) {
    std::strong_ordering order = std::strong_ordering::equal;
    switch (column) {
    case AssetColumn::Name:
        break;
    case AssetColumn::Folder:
        order = compareNames(a.asset->name, b.asset->name);
        break;
    case AssetColumn::Id:
        order = getIdKey(a.entry) <=> getIdKey(b.entry);
        break;
    case AssetColumn::Rooms:
        order = a.asset->roomCount <=> b.asset->roomCount;
        break;
    case AssetColumn::Source:
        order = static_cast<int>(a.asset->isExtracted) <=> static_cast<int>(b.asset->isExtracted);
        break;
    }
    return order != 0 ? order : compareNames(a.name, b.name);
}

std::strong_ordering compareObjects(const ObjectRow& a, const ObjectRow& b,
                                    const AssetColumn column) {
    std::strong_ordering order = std::strong_ordering::equal;
    if (column == AssetColumn::Id) {
        order = getIdKey(a.entry) <=> getIdKey(b.entry);
    } else if (column == AssetColumn::Source) {
        order = static_cast<int>(a.asset->isExtracted) <=> static_cast<int>(b.asset->isExtracted);
    }
    return order != 0 ? order : compareNames(a.asset->name, b.asset->name);
}

} // namespace

std::vector<SceneRow> makeSceneRows(const hackeroot::Workspace& workspace,
                                    const std::string_view filter) {
    const hackeroot::Tables& tables = workspace.getTables();
    std::vector<SceneRow> rows;
    for (const hackeroot::AssetDirectory& scene : workspace.getAssets().scenes) {
        const hackeroot::SceneEntry* entry = findSceneEntry(tables, scene.name);
        SceneRow row{
            .asset = &scene,
            .name = entry != nullptr ? makeReadableSceneName(entry->enumName) : scene.name,
            .category = getAssetCategory(scene),
            .entry = entry,
        };
        const bool matches = containsIgnoringCase(row.name, filter) ||
                             containsIgnoringCase(scene.name, filter) ||
                             (entry != nullptr && containsIgnoringCase(entry->enumName, filter));
        if (!matches) {
            continue;
        }
        if (entry != nullptr) {
            row.entrances = static_cast<usize>(std::ranges::count(
                tables.entrances, entry->enumName, &hackeroot::EntranceEntry::sceneEnumName));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<ObjectRow> makeObjectRows(const hackeroot::Workspace& workspace,
                                      const std::string_view filter) {
    const std::vector<hackeroot::ObjectEntry>& entries = workspace.getTables().objects;
    std::vector<ObjectRow> rows;
    for (const hackeroot::AssetDirectory& object : workspace.getAssets().objects) {
        const auto found = std::ranges::find(entries, object.name, &hackeroot::ObjectEntry::name);
        const hackeroot::ObjectEntry* entry = found != entries.end() ? &*found : nullptr;
        const bool matches = containsIgnoringCase(object.name, filter) ||
                             (entry != nullptr && containsIgnoringCase(entry->enumName, filter));
        if (matches) {
            rows.push_back(ObjectRow{.asset = &object, .entry = entry});
        }
    }
    return rows;
}

std::vector<SceneRow> sortSceneRows(std::vector<SceneRow> rows, const AssetColumn column,
                                    const bool ascending) {
    std::ranges::stable_sort(rows, [column, ascending](const SceneRow& a, const SceneRow& b) {
        if (a.category != b.category) {
            return a.category < b.category;
        }
        const std::strong_ordering order = compareScenes(a, b, column);
        return ascending ? order < 0 : order > 0;
    });
    return rows;
}

std::vector<ObjectRow> sortObjectRows(std::vector<ObjectRow> rows, const AssetColumn column,
                                      const bool ascending) {
    std::ranges::stable_sort(rows, [column, ascending](const ObjectRow& a, const ObjectRow& b) {
        const std::strong_ordering order = compareObjects(a, b, column);
        return ascending ? order < 0 : order > 0;
    });
    return rows;
}

} // namespace toki::assets
