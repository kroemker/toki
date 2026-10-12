#pragma once

#include "app/core/ui/Fonts.h"
#include "app/core/ui/Panel.h"
#include "app/displayList/Types.h"
#include "app/displayList/domain/FrameInspection.h"
#include "app/viewport/ui/ViewportPanel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "gfx/Device.h"
#include "gfx/ImageTexture.h"
#include "hackeroot/Workspace.h"
#include "n64/Rdp.h"
#include "z64/GbiNames.h"

#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace toki::displayList {

/// The display lists the viewport's frame ran, for reading and learning how a
/// frame is drawn: every command in the order the RSP ran it, nested by the
/// calls between lists, as `gbi.h` writes it, with what each command does and
/// what it did in this frame. It follows the viewport, or holds one frame.
class DisplayListPanel : public core::Panel {
public:
    DisplayListPanel(const gfx::Device& device, viewport::ViewportPanel& viewportPanel,
                     const workspace::WorkspaceService& workspaceService, const core::Fonts& fonts);

    [[nodiscard]] bool isOpenByDefault() const override {
        return false;
    }

    void draw() override;

    /// Keeps the viewport's highlight on the selected command's triangles, and
    /// clears it while the panel is closed. Runs every frame, open or not.
    void updateHighlight();

private:
    struct VisibleRow {
        usize row = 0;
        std::optional<usize> group;
    };

    void updateNames();
    void updateFrame();
    void listVisibleRows();
    void drawToolbar();
    void drawRows();
    void drawRow(const VisibleRow& visible);
    void drawDetails();
    void drawFields(usize row);
    void drawState(usize row);
    void drawTilePreviews(usize row, const n64::RdpState& state);
    [[nodiscard]] const std::string& findRowText(usize row);
    [[nodiscard]] z64::FindAddressName makeAddressNamer(usize row) const;

    struct UploadedTile {
        usize tile = 0;
        gfx::ImageTexture texture;
    };

    const gfx::Device& device;
    viewport::ViewportPanel& viewportPanel;
    const workspace::WorkspaceService& workspaceService;
    const core::Fonts& fonts;
    const hackeroot::Workspace* namedWorkspace = nullptr;
    std::future<std::optional<z64::GbiNames>> readingNames;
    std::optional<z64::GbiNames> names;
    z64::GbiNames plainNames;
    std::unique_ptr<viewport::TracedFrame> shownFrame;
    std::unique_ptr<FrameInspection> inspection;
    bool isFollowing = true;
    double refreshedAt = 0.0;
    std::string filter;
    std::set<usize> expanded;
    std::set<usize> expandedGroups;
    std::vector<VisibleRow> visibleRows;
    bool areRowsStale = true;
    std::optional<usize> selected;
    std::map<usize, std::string> rowTexts;
    std::vector<UploadedTile> previews;
    std::optional<usize> previewRow;
    u64 previewGeneration = 0;
    std::optional<usize> highlightRow;
    u64 highlightGeneration = 0;
};

} // namespace toki::displayList
