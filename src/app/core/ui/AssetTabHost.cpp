#include "AssetTabHost.h"

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

AssetTabHost::AssetTabHost(std::string emptyText, UnsavedEditsDialog& unsavedEditsDialog)
    : emptyText(std::move(emptyText)), unsavedEditsDialog(unsavedEditsDialog) {}

ImGuiID AssetTabHost::findDockId(const std::optional<std::string>& key) const {
    if (!key) {
        return 0;
    }
    const auto found = slots.find(*key);
    return found != slots.end() ? found->second.dockId : 0;
}

void AssetTabHost::open(const std::string_view key, const TabOpening opening,
                        const MakeTab& makeTab) {
    const OpenedTab opened = assetTabs.open(key, opening);
    const ImGuiID dockId = findDockId(opened.replaced ? opened.replaced : opened.beside);
    if (opened.replaced) {
        if (const auto replaced = slots.find(*opened.replaced); replaced != slots.end()) {
            retired.push_back(std::move(replaced->second.tab));
            slots.erase(replaced);
        }
    }
    if (opened.isNew) {
        slots.insert_or_assign(std::string(key), Slot{.tab = makeTab(), .dockOnOpen = dockId});
    }
    focusing = std::string(key);
}

AssetTab* AssetTabHost::findActive() const {
    const std::optional<std::string>& active = assetTabs.getActive();
    if (!active) {
        return nullptr;
    }
    const auto found = slots.find(*active);
    return found != slots.end() ? found->second.tab.get() : nullptr;
}

void AssetTabHost::saveTab(const std::string& key, AssetTab::Saved saved) {
    const auto found = slots.find(key);
    if (found == slots.end() || isSavingTab) {
        if (saved) {
            saved(false);
        }
        return;
    }
    isSavingTab = true;
    found->second.tab->save([this, saved = std::move(saved)](const bool isWritten) {
        isSavingTab = false;
        if (saved) {
            saved(isWritten);
        }
    });
}

void AssetTabHost::saveActive() {
    const std::optional<std::string>& active = assetTabs.getActive();
    if (active && !isSavingTab) {
        saveTab(*active, nullptr);
    }
}

void AssetTabHost::saveAll(AssetTab::Saved saved) {
    std::vector<std::string> keys;
    for (const AssetTabEntry& entry : assetTabs.getTabs()) {
        if (slots.at(entry.key).tab->isEdited()) {
            keys.push_back(entry.key);
        }
    }
    saveNext(std::move(keys), true, std::move(saved));
}

void AssetTabHost::saveNext(std::vector<std::string> keys, const bool isAllWritten,
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

void AssetTabHost::closeActive() {
    if (const std::optional<std::string> active = assetTabs.getActive()) {
        requestClose(*active);
    }
}

void AssetTabHost::requestClose(const std::string& key) {
    const auto found = slots.find(key);
    if (found == slots.end()) {
        return;
    }
    if (!found->second.tab->isEdited()) {
        closeUnasked(key);
        return;
    }
    focusing = key;
    unsavedEditsDialog.ask({found->second.tab->getTitle()}, "closing",
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

void AssetTabHost::closeUnasked(const std::string_view key) {
    const auto found = slots.find(key);
    if (found == slots.end()) {
        return;
    }
    found->second.tab->discard();
    assetTabs.close(key);
    retired.push_back(std::move(found->second.tab));
    slots.erase(found);
    if (focusing == key) {
        focusing.reset();
    }
}

void AssetTabHost::closeAllUnasked() {
    while (const std::optional<std::string> active = assetTabs.getActive()) {
        closeUnasked(*active);
    }
}

std::vector<std::string> AssetTabHost::listEditedTitles() const {
    std::vector<std::string> titles;
    for (const AssetTabEntry& entry : assetTabs.getTabs()) {
        const AssetTab& tab = *slots.at(entry.key).tab;
        if (tab.isEdited()) {
            titles.push_back(tab.getTitle());
        }
    }
    return titles;
}

void AssetTabHost::drawTab(const AssetTabEntry& entry, const ImGuiID centralNode) {
    Slot& slot = slots.at(entry.key);
    AssetTab& tab = *slot.tab;
    if (slot.dockOnOpen) {
        ImGui::SetNextWindowDockID(*slot.dockOnOpen != 0 ? *slot.dockOnOpen : centralNode,
                                   ImGuiCond_Always);
        slot.dockOnOpen.reset();
    }
    if (focusing == entry.key) {
        ImGui::SetNextWindowFocus();
        focusing.reset();
    }
    const std::string label = std::format("{}###asset:{}", tab.getTitle(), entry.key);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    if (tab.isEdited()) {
        flags |= ImGuiWindowFlags_UnsavedDocument;
    }
    bool isOpen = true;
    const bool isVisible = ImGui::Begin(label.c_str(), &isOpen, flags);
    slot.dockId = ImGui::GetWindowDockID();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        assetTabs.activate(entry.key);
        hasFocus = true;
    }
    if (isVisible) {
        tab.draw();
    }
    ImGui::End();
    if (!isOpen) {
        closing.push_back(entry.key);
    }
}

void AssetTabHost::draw(const ImGuiID centralNode) {
    retired.clear();
    hasFocus = false;
    for (const AssetTabEntry& entry : assetTabs.getTabs()) {
        if (entry.isPreview && slots.at(entry.key).tab->isEdited()) {
            assetTabs.pin(entry.key);
        }
    }
    const std::vector<AssetTabEntry> entries = assetTabs.getTabs();
    for (const AssetTabEntry& entry : entries) {
        drawTab(entry, centralNode);
    }
    const std::vector<std::string> closed = std::move(closing);
    closing.clear();
    for (const std::string& key : closed) {
        requestClose(key);
    }
}

std::optional<u64> AssetTabHost::findSelectionKey() const {
    const AssetTab* active = findActive();
    return active != nullptr ? active->findSelectionKey() : std::nullopt;
}

void AssetTabHost::drawProperties() {
    if (AssetTab* active = findActive()) {
        active->drawProperties();
        return;
    }
    ImGui::TextColored(palette::TextMuted, "%s", emptyText.c_str());
}

} // namespace toki::core
