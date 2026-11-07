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

void AssetTabs::makeActive(const std::string_view key) {
    std::erase(recent, key);
    recent.emplace_back(key);
    active = std::string(key);
}

OpenedTab AssetTabs::open(const std::string_view key, const TabOpening opening) {
    const bool isPreview = opening == TabOpening::Preview;
    if (const std::optional<usize> index = findIndex(key)) {
        if (!isPreview) {
            tabs[*index].isPreview = false;
        }
        makeActive(key);
        return OpenedTab{};
    }
    OpenedTab opened{.isNew = true};
    const auto preview = std::ranges::find_if(tabs, &AssetTabEntry::isPreview);
    if (isPreview && preview != tabs.end()) {
        std::erase(recent, preview->key);
        opened.replaced = std::exchange(preview->key, std::string(key));
    } else {
        opened.beside = active;
        tabs.push_back(AssetTabEntry{.key = std::string(key), .isPreview = isPreview});
    }
    makeActive(key);
    return opened;
}

void AssetTabs::pin(const std::string_view key) {
    if (const std::optional<usize> index = findIndex(key)) {
        tabs[*index].isPreview = false;
    }
}

void AssetTabs::activate(const std::string_view key) {
    if (findIndex(key) && active != key) {
        makeActive(key);
    }
}

bool AssetTabs::close(const std::string_view key) {
    const std::optional<usize> index = findIndex(key);
    if (!index) {
        return false;
    }
    tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(*index));
    std::erase(recent, key);
    if (active == key) {
        active = recent.empty() ? std::nullopt : std::optional<std::string>(recent.back());
    }
    return true;
}

} // namespace toki::core
