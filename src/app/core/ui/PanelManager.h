#pragma once

#include "app/core/ui/Panel.h"

#include <imgui.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
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

    /// The panel whose window had the focus last, a menu or popup not
    /// counted; empty before any had.
    [[nodiscard]] const std::string& getLastFocusedId() const {
        return lastFocusedId;
    }

    /// Records a window that is not a panel, an asset tab's, as focused last.
    void setLastFocusedId(std::string id) {
        lastFocusedId = std::move(id);
    }

    /// The dock node in the middle of the panels, where the viewport is by
    /// default; valid once `draw` ran.
    [[nodiscard]] ImGuiID findCentralNodeId() const;

private:
    void buildDefaultLayout() const;
    [[nodiscard]] bool hasUnplacedPanel() const;

    std::vector<std::unique_ptr<Panel>> panels;
    bool rebuildLayout = false;
    bool checkedSavedLayout = false;
    std::string lastFocusedId;
    ImGuiID dockspaceId = 0;
};

} // namespace toki::core
