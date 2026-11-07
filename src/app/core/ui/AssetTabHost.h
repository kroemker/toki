#pragma once

#include "app/core/domain/AssetTabs.h"
#include "app/core/ui/AssetTab.h"
#include "app/core/ui/PropertySource.h"
#include "app/core/ui/UnsavedEditsDialog.h"

#include <imgui.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

/// The assets open in tabs, as an IDE shows open files: each tab an
/// `AssetTab` drawn in a window of its own, docked among the panels. A new
/// tab joins the group of the tab in use, or the central node when there is
/// none, and a preview takes the place of the one before it. A tab with edits
/// no save wrote is marked as unsaved, and closing it asks first. The
/// Properties panel shows the active tab's asset.
class AssetTabHost : public PropertySource {
public:
    using MakeTab = std::function<std::unique_ptr<AssetTab>()>;

    /// `emptyText` is what the Properties panel says while no tab is open.
    AssetTabHost(std::string emptyText, UnsavedEditsDialog& unsavedEditsDialog);

    /// Opens the asset `key`, made by `makeTab` when it is not open yet, and
    /// focuses its tab.
    void open(std::string_view key, TabOpening opening, const MakeTab& makeTab);

    [[nodiscard]] AssetTab* findActive() const;

    [[nodiscard]] const std::optional<std::string>& getActiveKey() const {
        return assetTabs.getActive();
    }

    /// Saves the active tab's asset.
    void saveActive();

    /// Saves every tab with edits, one after another, and calls `saved` with
    /// whether all of them were written.
    void saveAll(AssetTab::Saved saved = nullptr);

    /// Closes the active tab, asking first when it has edits.
    void closeActive();

    /// Closes the tab of `key` without asking, its edits thrown away.
    void closeUnasked(std::string_view key);

    /// Closes every tab without asking, as when what they show is gone.
    void closeAllUnasked();

    /// The titles of the tabs with edits, in their order.
    [[nodiscard]] std::vector<std::string> listEditedTitles() const;

    [[nodiscard]] bool isSaving() const {
        return isSavingTab;
    }

    /// Draws every tab's window. A new one is docked into `centralNode` when
    /// it has no group to join.
    void draw(ImGuiID centralNode);

    /// Whether a tab's window had the focus in the frame drawn last.
    [[nodiscard]] bool isFocused() const {
        return hasFocus;
    }

    [[nodiscard]] std::optional<u64> findSelectionKey() const override;
    void drawProperties() override;

private:
    struct Slot {
        std::unique_ptr<AssetTab> tab;
        /// The dock node the window was in when it was drawn last; none when
        /// it floats.
        ImGuiID dockId = 0;
        /// The node to dock the window into when it is first drawn; 0 for the
        /// central node.
        std::optional<ImGuiID> dockOnOpen;
    };

    void requestClose(const std::string& key);
    void saveTab(const std::string& key, AssetTab::Saved saved);
    void saveNext(std::vector<std::string> keys, bool isAllWritten, AssetTab::Saved saved);
    void drawTab(const AssetTabEntry& entry, ImGuiID centralNode);
    [[nodiscard]] ImGuiID findDockId(const std::optional<std::string>& key) const;

    std::string emptyText;
    UnsavedEditsDialog& unsavedEditsDialog;
    AssetTabs assetTabs;
    std::map<std::string, Slot, std::less<>> slots;
    /// The tab to focus when it is drawn next, once, after it was opened.
    std::optional<std::string> focusing;
    std::vector<std::string> closing;
    /// Tabs closed this frame, kept until the tabs are drawn next: what they
    /// drew this frame may still show textures they own.
    std::vector<std::unique_ptr<AssetTab>> retired;
    bool isSavingTab = false;
    bool hasFocus = false;
};

} // namespace toki::core
