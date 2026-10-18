#pragma once

#include "app/core/domain/Settings.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/PanelManager.h"

#include <memory>
#include <vector>

namespace toki::core {

struct MenuActions {
    bool changeHackerootPath = false;
    bool openSettings = false;
    bool saveScene = false;
    bool build = false;
    bool run = false;
    bool stopBuild = false;
    bool quit = false;
    bool undo = false;
    bool redo = false;
};

/// What the menus can do now.
struct MenuState {
    bool buildRunning = false;
    bool sceneOpen = false;
    bool canUndo = false;
    bool canRedo = false;
};

[[nodiscard]] MenuActions drawMenuBar(PanelManager& panels,
                                      const std::vector<std::unique_ptr<Dialog>>& dialogs,
                                      const Settings& settings, const MenuState& state);

} // namespace toki::core
