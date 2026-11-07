#pragma once

#include "app/core/Types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

/// One asset open in a tab, by the key its editor names it with.
struct AssetTabEntry {
    std::string key;
    /// Whether the tab only previews the asset: the next asset previewed
    /// takes its place, until it is pinned.
    bool isPreview = false;
};

/// How an asset is opened: to look at it, in the preview tab, or to keep it
/// open, in a tab of its own.
enum class TabOpening : u8 {
    Preview,
    Pinned,
};

/// What opening an asset did to the tabs.
struct OpenedTab {
    /// Whether the asset was not open before.
    bool isNew = false;
    /// The asset whose preview tab the opened one took.
    std::optional<std::string> replaced;
};

/// The assets open in tabs, as an IDE keeps the files it has open: in the
/// order they were opened, one of them active, and at most one previewed.
/// A preview is taken over by the next asset previewed, so looking through
/// assets one after another leaves one tab; pinning it, as an edit does,
/// keeps it.
class AssetTabs {
public:
    /// Opens the asset `key` and makes it active. One already open is only
    /// made active, and pinned when it is opened pinned. A new one takes the
    /// place of the preview, when it is previewed and there is one, and is
    /// put after the active tab otherwise.
    OpenedTab open(std::string_view key, TabOpening opening);

    /// Keeps the tab of `key` open when another asset is previewed.
    void pin(std::string_view key);

    void activate(std::string_view key);

    /// Closes the tab of `key`. The tab after it becomes active when it was,
    /// or the one before it when it was the last. Returns whether it was open.
    bool close(std::string_view key);

    [[nodiscard]] const std::vector<AssetTabEntry>& getTabs() const {
        return tabs;
    }

    [[nodiscard]] const std::optional<std::string>& getActive() const {
        return active;
    }

    [[nodiscard]] const AssetTabEntry* findTab(std::string_view key) const;

private:
    [[nodiscard]] std::optional<usize> findIndex(std::string_view key) const;

    std::vector<AssetTabEntry> tabs;
    std::optional<std::string> active;
};

} // namespace toki::core
