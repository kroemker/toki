#pragma once

#include "app/core/domain/AssetTabs.h"
#include "app/core/ui/AssetTab.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/PropertySource.h"
#include "app/core/ui/UnsavedEditsDialog.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

/// A panel of assets open in tabs, as an IDE shows open files: each tab an
/// `AssetTab`, its title marked while it has edits no save wrote, the one
/// previewed in faint text. Closing a tab with edits asks first. The
/// Properties panel shows the active tab's asset.
class AssetTabPanel : public Panel, public PropertySource {
public:
    using MakeTab = std::function<std::unique_ptr<AssetTab>()>;

    /// `emptyText` is what the panel says while no tab is open.
    AssetTabPanel(std::string id, std::string title, const char* icon, DockSlot slot,
                  std::string emptyText, UnsavedEditsDialog& unsavedEditsDialog);

    /// Opens the asset `key`, made by `makeTab` when it is not open yet, and
    /// brings the panel to the front.
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

    void draw() override;

    [[nodiscard]] std::optional<u64> findSelectionKey() const override;
    void drawProperties() override;

private:
    void requestClose(const std::string& key);
    void saveTab(const std::string& key, AssetTab::Saved saved);
    void saveNext(std::vector<std::string> keys, bool isAllWritten, AssetTab::Saved saved);
    void drawTab(const AssetTabEntry& entry);

    std::string emptyText;
    UnsavedEditsDialog& unsavedEditsDialog;
    AssetTabs assetTabs;
    std::map<std::string, std::unique_ptr<AssetTab>, std::less<>> tabs;
    /// The tab the tab bar is to select, once, after it was opened or another
    /// was closed.
    std::optional<std::string> selecting;
    std::vector<std::string> closing;
    /// Tabs closed this frame, kept until the panel is drawn next: what they
    /// drew this frame may still show textures they own.
    std::vector<std::unique_ptr<AssetTab>> retired;
    bool isSavingTab = false;
};

} // namespace toki::core
