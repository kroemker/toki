#pragma once

#include "app/core/domain/Settings.h"
#include "app/core/ui/Dialog.h"
#include "app/core/ui/PanelManager.h"

#include <memory>
#include <string>
#include <vector>

namespace toki::core {

struct MenuActions {
    bool changeHackerootPath = false;
    bool openSettings = false;
    bool save = false;
    bool saveAll = false;
    bool closeTab = false;
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
    /// What File > Save saves, `Save Message 0x1234`; empty when nothing is
    /// open to save.
    std::string saveLabel;
    bool canSaveAll = false;
    bool canCloseTab = false;
    bool canUndo = false;
    bool canRedo = false;
};

[[nodiscard]] MenuActions drawMenuBar(PanelManager& panels,
                                      const std::vector<std::unique_ptr<Dialog>>& dialogs,
                                      const Settings& settings, const MenuState& state);

} // namespace toki::core
