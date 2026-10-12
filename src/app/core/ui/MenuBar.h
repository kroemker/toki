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
    bool build = false;
    bool run = false;
    bool stopBuild = false;
    bool quit = false;
};

[[nodiscard]] MenuActions drawMenuBar(PanelManager& panels,
                                      const std::vector<std::unique_ptr<Dialog>>& dialogs,
                                      const Settings& settings, bool buildRunning);

} // namespace toki::core
