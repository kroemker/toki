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

    [[nodiscard]] std::vector<std::string> getOpenIds() const;

    [[nodiscard]] std::vector<std::string> getClosedIds() const;

    /// Opens the panels in `open` and closes those in `closed`. A panel in
    /// neither is new to the saved state and opens as its default says.
    void restoreOpenState(const std::vector<std::string>& open,
                          const std::vector<std::string>& closed);

    void openDefaults();

private:
    void buildDefaultLayout(ImGuiID dockspaceId) const;

    std::vector<std::unique_ptr<Panel>> panels;
    bool rebuildLayout = false;
};

} // namespace toki::core
