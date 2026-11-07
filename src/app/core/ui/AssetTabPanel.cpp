#include "AssetTabPanel.h"

#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::core {

AssetTabPanel::AssetTabPanel(std::string id, std::string title, const char* icon,
                             const DockSlot slot, std::string emptyText,
                             UnsavedEditsDialog& unsavedEditsDialog)
    : Panel(std::move(id), std::move(title), icon, slot), emptyText(std::move(emptyText)),
      unsavedEditsDialog(unsavedEditsDialog) {}

void AssetTabPanel::open(const std::string_view key, const TabOpening opening,
                         const MakeTab& makeTab) {
    const OpenedTab opened = assetTabs.open(key, opening);
    if (opened.replaced) {
        if (const auto replaced = tabs.find(*opened.replaced); replaced != tabs.end()) {
            retired.push_back(std::move(replaced->second));
            tabs.erase(replaced);
        }
    }
    if (opened.isNew) {
        tabs.insert_or_assign(std::string(key), makeTab());
    }
    selecting = std::string(key);
    setOpen(true);
    ImGui::SetWindowFocus(getWindowTitle().c_str());
}

AssetTab* AssetTabPanel::findActive() const {
    const std::optional<std::string>& active = assetTabs.getActive();
    if (!active) {
        return nullptr;
    }
    const auto found = tabs.find(*active);
    return found != tabs.end() ? found->second.get() : nullptr;
}

void AssetTabPanel::saveTab(const std::string& key, AssetTab::Saved saved) {
    const auto found = tabs.find(key);
    if (found == tabs.end() || isSavingTab) {
        if (saved) {
            saved(false);
        }
        return;
    }
    isSavingTab = true;
    found->second->save([this, saved = std::move(saved)](const bool isWritten) {
        isSavingTab = false;
        if (saved) {
            saved(isWritten);
        }
    });
}

void AssetTabPanel::saveActive() {
    const std::optional<std::string>& active = assetTabs.getActive();
    if (active && !isSavingTab) {
        saveTab(*active, nullptr);
    }
}

void AssetTabPanel::saveAll(AssetTab::Saved saved) {
    std::vector<std::string> keys;
    for (const AssetTabEntry& entry : assetTabs.getTabs()) {
        if (tabs.at(entry.key)->isEdited()) {
            keys.push_back(entry.key);
        }
    }
    saveNext(std::move(keys), true, std::move(saved));
}

void AssetTabPanel::saveNext(std::vector<std::string> keys, const bool isAllWritten,
                             AssetTab::Saved saved) {
    if (keys.empty() || !isAllWritten) {
        if (saved) {
            saved(isAllWritten);
        }
        return;
    }
    const std::string key = keys.front();
    keys.erase(keys.begin());
    saveTab(key, [this, keys = std::move(keys), saved = std::move(saved)](const bool isWritten) {
        saveNext(keys, isWritten, saved);
    });
}

void AssetTabPanel::closeActive() {
    if (const std::optional<std::string> active = assetTabs.getActive()) {
        requestClose(*active);
    }
}

void AssetTabPanel::requestClose(const std::string& key) {
    const auto found = tabs.find(key);
    if (found == tabs.end()) {
        return;
    }
    if (!found->second->isEdited()) {
        closeUnasked(key);
        return;
    }
    unsavedEditsDialog.ask({found->second->getTitle()}, "closing",
                           [this, key](const UnsavedChoice choice) {
                               if (choice == UnsavedChoice::Save) {
                                   saveTab(key, [this, key](const bool isWritten) {
                                       if (isWritten) {
                                           closeUnasked(key);
                                       }
                                   });
                               } else if (choice == UnsavedChoice::DontSave) {
                                   closeUnasked(key);
                               }
                           });
}

void AssetTabPanel::closeUnasked(const std::string_view key) {
    const auto found = tabs.find(key);
    if (found == tabs.end()) {
        return;
    }
    found->second->discard();
    assetTabs.close(key);
    retired.push_back(std::move(found->second));
    tabs.erase(found);
    selecting = assetTabs.getActive();
}

void AssetTabPanel::closeAllUnasked() {
    while (const std::optional<std::string> active = assetTabs.getActive()) {
        closeUnasked(*active);
    }
}

std::vector<std::string> AssetTabPanel::listEditedTitles() const {
    std::vector<std::string> titles;
    for (const AssetTabEntry& entry : assetTabs.getTabs()) {
        const AssetTab& tab = *tabs.at(entry.key);
        if (tab.isEdited()) {
            titles.push_back(tab.getTitle());
        }
    }
    return titles;
}

void AssetTabPanel::drawTab(const AssetTabEntry& entry) {
    AssetTab& tab = *tabs.at(entry.key);
    const std::string title = tab.getTitle();
    const bool isEdited = tab.isEdited();
    const std::string label = std::format("{}{}###{}", title, isEdited ? "  " : "", entry.key);
    const ImGuiTabItemFlags flags =
        selecting == entry.key ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    if (entry.isPreview) {
        ImGui::PushStyleColor(ImGuiCol_Text, palette::TextMuted);
    }
    bool isOpen = true;
    const bool isSelected = ImGui::BeginTabItem(label.c_str(), &isOpen, flags);
    if (entry.isPreview) {
        ImGui::PopStyleColor();
    }
    if (isEdited) {
        const ImVec2 corner = ImGui::GetItemRectMin();
        const ImVec2 padding = ImGui::GetStyle().FramePadding;
        const float x = corner.x + padding.x + ImGui::CalcTextSize(title.c_str()).x +
                        (ImGui::GetFontSize() * 0.2F);
        ImGui::GetWindowDrawList()->AddText(ImVec2(x, corner.y + padding.y),
                                            ImGui::GetColorU32(palette::Warning), "*");
    }
    if (ImGui::IsItemHovered()) {
        if (entry.isPreview && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            assetTabs.pin(entry.key);
        }
        if (entry.isPreview) {
            ImGui::SetItemTooltip("Previewed: the next one picked takes this tab. Double-click "
                                  "it, or edit it, to keep it open.");
        } else if (isEdited) {
            ImGui::SetItemTooltip("Edited, not saved.");
        }
    }
    if (!isOpen) {
        closing.push_back(entry.key);
    }
    if (!isSelected) {
        return;
    }
    if (!selecting || selecting == entry.key) {
        assetTabs.activate(entry.key);
        selecting.reset();
        tab.draw();
    }
    ImGui::EndTabItem();
}

void AssetTabPanel::draw() {
    retired.clear();
    for (const AssetTabEntry& entry : assetTabs.getTabs()) {
        if (entry.isPreview && tabs.at(entry.key)->isEdited()) {
            assetTabs.pin(entry.key);
        }
    }
    if (tabs.empty()) {
        ImGui::TextColored(palette::TextMuted, "%s", emptyText.c_str());
        return;
    }
    if (ImGui::BeginTabBar("##assetTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        const std::vector<AssetTabEntry> entries = assetTabs.getTabs();
        for (const AssetTabEntry& entry : entries) {
            drawTab(entry);
        }
        ImGui::EndTabBar();
    }
    const std::vector<std::string> closed = std::move(closing);
    closing.clear();
    for (const std::string& key : closed) {
        requestClose(key);
    }
}

std::optional<u64> AssetTabPanel::findSelectionKey() const {
    const AssetTab* active = findActive();
    return active != nullptr ? active->findSelectionKey() : std::nullopt;
}

void AssetTabPanel::drawProperties() {
    if (AssetTab* active = findActive()) {
        active->drawProperties();
        return;
    }
    ImGui::TextColored(palette::TextMuted, "%s", emptyText.c_str());
}

} // namespace toki::core
