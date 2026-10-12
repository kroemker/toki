#include "MenuBar.h"

#include "app/core/domain/Settings.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/PanelManager.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <memory>

namespace toki::core {

namespace {

void drawFileMenu(const Settings& settings, MenuActions& actions) {
    if (!ImGui::BeginMenu("File")) {
        return;
    }

    if (ImGui::MenuItem("Set HackerOoT folder…")) {
        actions.changeHackerootPath = true;
    }

    ImGui::BeginDisabled();
    ImGui::TextUnformatted(settings.hackerootPath ? encodeUtf8(*settings.hackerootPath).c_str()
                                                  : "No folder set");
    ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::MenuItem("Settings…", "Ctrl+,")) {
        actions.openSettings = true;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Quit", "Alt+F4")) {
        actions.quit = true;
    }
    ImGui::EndMenu();
}

void drawBuildMenu(const bool buildRunning, MenuActions& actions) {
    if (!ImGui::BeginMenu("Build")) {
        return;
    }
    if (ImGui::MenuItem("Build", "Ctrl+B", false, !buildRunning)) {
        actions.build = true;
    }
    if (ImGui::MenuItem("Build and run", "F5", false, !buildRunning)) {
        actions.run = true;
    }
    if (ImGui::MenuItem("Stop", nullptr, false, buildRunning)) {
        actions.stopBuild = true;
    }
    ImGui::EndMenu();
}

void drawViewMenu(PanelManager& panels) {
    if (!ImGui::BeginMenu("View")) {
        return;
    }

    for (const std::unique_ptr<Panel>& panel : panels.getPanels()) {
        bool open = panel->isOpen();
        if (!panel->isTool() && ImGui::MenuItem(panel->getMenuLabel().c_str(), nullptr, &open)) {
            panel->setOpen(open);
        }
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Reset layout")) {
        panels.openDefaults();
        panels.requestDefaultLayout();
    }
    ImGui::EndMenu();
}

void drawToolsMenu(PanelManager& panels) {
    if (!ImGui::BeginMenu("Tools")) {
        return;
    }
    for (const std::unique_ptr<Panel>& panel : panels.getPanels()) {
        bool open = panel->isOpen();
        if (panel->isTool() && ImGui::MenuItem(panel->getMenuLabel().c_str(), nullptr, &open)) {
            panel->setOpen(open);
        }
    }
    ImGui::EndMenu();
}

} // namespace

MenuActions drawMenuBar(PanelManager& panels, const Settings& settings, const bool buildRunning) {
    MenuActions actions;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(space::Medium, space::Medium));
    const bool visible = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    if (!visible) {
        return actions;
    }

    drawFileMenu(settings, actions);
    drawBuildMenu(buildRunning, actions);
    drawToolsMenu(panels);
    drawViewMenu(panels);

    ImGui::EndMainMenuBar();
    return actions;
}

} // namespace toki::core
