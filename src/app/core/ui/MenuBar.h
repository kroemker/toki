#pragma once

#include "app/core/domain/Settings.h"
#include "app/core/ui/PanelManager.h"

namespace toki::core {

struct MenuActions {
    bool changeHackerootPath = false;
    bool openSettings = false;
    bool build = false;
    bool run = false;
    bool stopBuild = false;
    bool quit = false;
};

[[nodiscard]] MenuActions drawMenuBar(PanelManager& panels, const Settings& settings,
                                      bool buildRunning);

} // namespace toki::core
