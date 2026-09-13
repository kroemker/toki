#pragma once

#include "app/core/ui/Panel.h"

#include <imgui.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

class PanelManager {
public:
    void add(std::unique_ptr<Panel> panel);

    [[nodiscard]] Panel* find(std::string_view id);

    [[nodiscard]] const std::vector<std::unique_ptr<Panel>>& getPanels() const {
        return panels;
    }

    void draw();

    void requestDefaultLayout() {
        rebuildLayout = true;
    }

    [[nodiscard]] std::vector<std::string> openIds() const;

    void restoreOpenIds(const std::vector<std::string>& ids);

    void openDefaults();

private:
    void buildDefaultLayout(ImGuiID dockspaceId) const;

    std::vector<std::unique_ptr<Panel>> panels;
    bool rebuildLayout = false;
};

} // namespace toki::core
