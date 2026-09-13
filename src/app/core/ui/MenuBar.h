#pragma once

#include "app/core/domain/Settings.h"
#include "app/core/ui/PanelManager.h"

namespace toki::core {

struct MenuActions {
    bool changeHackerootPath = false;
    bool quit = false;
};

[[nodiscard]] MenuActions drawMenuBar(PanelManager& panels, const Settings& settings);

} // namespace toki::core
