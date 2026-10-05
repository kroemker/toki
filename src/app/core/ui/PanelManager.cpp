#include "PanelManager.h"

#include "app/core/ui/Panel.h"

#include <imgui_internal.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::core {

namespace {

constexpr float LeftFraction = 0.22F;
constexpr float RightFraction = 0.24F;
constexpr float BottomFraction = 0.28F;

} // namespace

void PanelManager::add(std::unique_ptr<Panel> panel) {
    panels.push_back(std::move(panel));
}

Panel* PanelManager::find(std::string_view id) {
    const auto match = std::ranges::find_if(
        panels, [id](const std::unique_ptr<Panel>& panel) { return panel->getId() == id; });
    return match == panels.end() ? nullptr : match->get();
}

std::vector<std::string> PanelManager::getOpenIds() const {
    std::vector<std::string> ids;
    for (const std::unique_ptr<Panel>& panel : panels) {
        if (panel->isOpen()) {
            ids.push_back(panel->getId());
        }
    }
    return ids;
}

std::vector<std::string> PanelManager::getClosedIds() const {
    std::vector<std::string> ids;
    for (const std::unique_ptr<Panel>& panel : panels) {
        if (!panel->isOpen()) {
            ids.push_back(panel->getId());
        }
    }
    return ids;
}

void PanelManager::restoreOpenState(const std::vector<std::string>& open,
                                    const std::vector<std::string>& closed) {
    for (const std::unique_ptr<Panel>& panel : panels) {
        if (std::ranges::find(open, panel->getId()) != open.end()) {
            panel->setOpen(true);
        } else if (std::ranges::find(closed, panel->getId()) != closed.end()) {
            panel->setOpen(false);
        } else {
            panel->setOpen(panel->isOpenByDefault());
        }
    }
}

void PanelManager::openDefaults() {
    for (const std::unique_ptr<Panel>& panel : panels) {
        panel->setOpen(panel->isOpenByDefault());
    }
}

void PanelManager::draw() {
    const ImGuiID dockspaceId =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);

    if (!checkedSavedLayout) {
        checkedSavedLayout = true;
        rebuildLayout = rebuildLayout || hasUnplacedPanel();
    }
    if (rebuildLayout) {
        buildDefaultLayout(dockspaceId);
        rebuildLayout = false;
    }

    for (const std::unique_ptr<Panel>& panel : panels) {
        if (!panel->isOpen()) {
            continue;
        }

        bool open = true;
        if (ImGui::Begin(panel->getWindowTitle().c_str(), &open, panel->getWindowFlags())) {
            panel->draw();
        }
        ImGui::End();
        panel->setOpen(open);
    }
}

bool PanelManager::hasUnplacedPanel() const {
    return std::ranges::any_of(panels, [](const std::unique_ptr<Panel>& panel) {
        return ImGui::FindWindowSettingsByID(ImHashStr(panel->getWindowTitle().c_str())) == nullptr;
    });
}

void PanelManager::buildDefaultLayout(ImGuiID dockspaceId) const {
    const auto wants = [this](DockSlot slot) {
        return std::ranges::any_of(panels, [slot](const std::unique_ptr<Panel>& panel) {
            return panel->getDockSlot() == slot;
        });
    };

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID centre = dockspaceId;
    ImGuiID left = centre;
    ImGuiID right = centre;
    ImGuiID bottom = centre;
    if (wants(DockSlot::Left)) {
        left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, LeftFraction, nullptr, &centre);
    }
    if (wants(DockSlot::Right)) {
        right =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, RightFraction, nullptr, &centre);
    }
    if (wants(DockSlot::Bottom)) {
        bottom =
            ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, BottomFraction, nullptr, &centre);
    }

    for (const std::unique_ptr<Panel>& panel : panels) {
        ImGuiID node = centre;
        switch (panel->getDockSlot()) {
        case DockSlot::Left:
            node = left;
            break;
        case DockSlot::Right:
            node = right;
            break;
        case DockSlot::Bottom:
            node = bottom;
            break;
        case DockSlot::Centre:
            break;
        }
        ImGui::DockBuilderDockWindow(panel->getWindowTitle().c_str(), node);
    }

    ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace toki::core
