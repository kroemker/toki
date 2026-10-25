#pragma once

#include "app/viewport/Types.h"
#include "app/viewport/domain/RenderMode.h"
#include "app/viewport/domain/ViewportChoices.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::viewport {

/// What is drawn over the frame, and whether any of it is.
struct ViewportOverlays {
    bool isShown = true;
    bool flagLinks = true;
    bool grid = false;
    /// Whether what the frame hides of an overlay is drawn, faintly.
    bool xray = false;
    /// How what is selected is shown: what it drew tinted and outlined, and a
    /// box around it.
    bool selectionHighlight = true;
    bool selectionBox = true;

    friend bool operator==(const ViewportOverlays&, const ViewportOverlays&) = default;
};

/// How the viewport draws, whatever it draws: chosen in its bar and kept
/// between sessions.
struct ViewportView {
    RenderMode renderMode;
    ViewportOverlays overlays;

    friend bool operator==(const ViewportView&, const ViewportView&) = default;
};

/// What `shading` is called where it is chosen.
[[nodiscard]] std::string_view getShadingLabel(Shading shading);

/// What `view`, and the content's `choices`, draw differently from the frame
/// the game would draw, each as a few words, for naming it over the image;
/// empty when nothing does.
[[nodiscard]] std::vector<std::string>
listViewChanges(const ViewportView& view, const std::optional<ViewportChoices>& choices = {});

} // namespace toki::viewport
