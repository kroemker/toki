#include "AssetTabs.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace toki::core {

std::optional<usize> AssetTabs::findIndex(const std::string_view key) const {
    const auto found = std::ranges::find(tabs, key, &AssetTabEntry::key);
    if (found == tabs.end()) {
        return std::nullopt;
    }
    return static_cast<usize>(found - tabs.begin());
}

const AssetTabEntry* AssetTabs::findTab(const std::string_view key) const {
    const std::optional<usize> index = findIndex(key);
    return index ? &tabs[*index] : nullptr;
}

OpenedTab AssetTabs::open(const std::string_view key, const TabOpening opening) {
    const bool isPreview = opening == TabOpening::Preview;
    if (const std::optional<usize> index = findIndex(key)) {
        if (!isPreview) {
            tabs[*index].isPreview = false;
        }
        active = std::string(key);
        return OpenedTab{};
    }
    OpenedTab opened{.isNew = true};
    const auto preview = std::ranges::find_if(tabs, &AssetTabEntry::isPreview);
    if (isPreview && preview != tabs.end()) {
        opened.replaced = std::move(preview->key);
        preview->key = std::string(key);
    } else {
        const std::optional<usize> after = active ? findIndex(*active) : std::nullopt;
        const usize at = after ? *after + 1 : tabs.size();
        tabs.insert(tabs.begin() + static_cast<std::ptrdiff_t>(at),
                    AssetTabEntry{.key = std::string(key), .isPreview = isPreview});
    }
    active = std::string(key);
    return opened;
}

void AssetTabs::pin(const std::string_view key) {
    if (const std::optional<usize> index = findIndex(key)) {
        tabs[*index].isPreview = false;
    }
}

void AssetTabs::activate(const std::string_view key) {
    if (findIndex(key)) {
        active = std::string(key);
    }
}

bool AssetTabs::close(const std::string_view key) {
    const std::optional<usize> index = findIndex(key);
    if (!index) {
        return false;
    }
    tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(*index));
    if (active != key) {
        return true;
    }
    if (tabs.empty()) {
        active.reset();
    } else {
        active = tabs[std::min(*index, tabs.size() - 1)].key;
    }
    return true;
}

} // namespace toki::core
