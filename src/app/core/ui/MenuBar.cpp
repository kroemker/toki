#include "MenuBar.h"

#include "app/core/domain/KeyBindings.h"
#include "app/core/domain/Settings.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/Hotkeys.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/PanelManager.h"
#include "app/core/ui/Theme.h"

#include <imgui.h>

#include <memory>
#include <vector>

namespace toki::core {

namespace {

void drawFileMenu(const Settings& settings, const MenuState& state, MenuActions& actions) {
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
    if (ImGui::MenuItem(state.saveLabel.empty() ? "Save" : state.saveLabel.c_str(),
                        getShortcutLabel(Action::Save).c_str(), false, !state.saveLabel.empty())) {
        actions.save = true;
    }
    if (ImGui::MenuItem("Save All", getShortcutLabel(Action::SaveAll).c_str(), false,
                        state.canSaveAll)) {
        actions.saveAll = true;
    }
    if (ImGui::MenuItem("Close Tab", getShortcutLabel(Action::CloseTab).c_str(), false,
                        state.canCloseTab)) {
        actions.closeTab = true;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Settings…", getShortcutLabel(Action::OpenSettings).c_str())) {
        actions.openSettings = true;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Quit", "Alt+F4")) {
        actions.quit = true;
    }
    ImGui::EndMenu();
}

void drawEditMenu(const MenuState& state, MenuActions& actions) {
    if (!ImGui::BeginMenu("Edit")) {
        return;
    }
    if (ImGui::MenuItem("Undo", getShortcutLabel(Action::Undo).c_str(), false, state.canUndo)) {
        actions.undo = true;
    }
    if (ImGui::MenuItem("Redo", getShortcutLabel(Action::Redo).c_str(), false, state.canRedo)) {
        actions.redo = true;
    }
    ImGui::EndMenu();
}

void drawBuildMenu(const bool buildRunning, MenuActions& actions) {
    if (!ImGui::BeginMenu("Build")) {
        return;
    }
    if (ImGui::MenuItem("Build", getShortcutLabel(Action::Build).c_str(), false, !buildRunning)) {
        actions.build = true;
    }
    if (ImGui::MenuItem("Build and run", getShortcutLabel(Action::Run).c_str(), false,
                        !buildRunning)) {
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
        if (ImGui::MenuItem(panel->getMenuLabel().c_str(), nullptr, &open)) {
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

void drawToolsMenu(const std::vector<std::unique_ptr<Dialog>>& dialogs) {
    if (!ImGui::BeginMenu("Tools")) {
        return;
    }
    for (const std::unique_ptr<Dialog>& dialog : dialogs) {
        if (ImGui::MenuItem(dialog->getMenuLabel().c_str(), nullptr, dialog->isShown())) {
            dialog->show();
        }
    }
    ImGui::EndMenu();
}

} // namespace

MenuActions drawMenuBar(PanelManager& panels, const std::vector<std::unique_ptr<Dialog>>& dialogs,
                        const Settings& settings, const MenuState& state) {
    MenuActions actions;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(space::Medium, space::Medium));
    const bool visible = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    if (!visible) {
        return actions;
    }

    drawFileMenu(settings, state, actions);
    drawEditMenu(state, actions);
    drawBuildMenu(state.buildRunning, actions);
    drawToolsMenu(dialogs);
    drawViewMenu(panels);

    ImGui::EndMainMenuBar();
    return actions;
}

} // namespace toki::core
