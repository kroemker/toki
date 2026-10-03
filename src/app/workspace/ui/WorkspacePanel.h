#pragma once

#include "app/core/ui/Panel.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <array>

namespace toki::workspace {

class WorkspacePanel : public core::Panel {
public:
    explicit WorkspacePanel(const WorkspaceService& workspaceService);

    void draw() override;

private:
    void drawTables(const hackeroot::Workspace& workspace) const;

    const WorkspaceService& workspaceService;
    std::array<char, 128> filter{};
};

} // namespace toki::workspace
