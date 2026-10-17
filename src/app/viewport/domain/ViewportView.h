#pragma once

#include "app/viewport/Types.h"

#include <string>
#include <vector>

namespace toki::viewport {

/// What is drawn over the frame, and whether any of it is.
struct ViewportOverlays {
    bool isShown = true;
    bool flagLinks = true;

    friend bool operator==(const ViewportOverlays&, const ViewportOverlays&) = default;
};

/// How the viewport draws, whatever it draws: chosen in its bar and kept
/// between sessions.
struct ViewportView {
    ViewportOverlays overlays;

    friend bool operator==(const ViewportView&, const ViewportView&) = default;
};

/// What `view` draws differently from the frame the game would draw, each as
/// a few words, for naming it over the image; empty when nothing does.
[[nodiscard]] std::vector<std::string> listViewChanges(const ViewportView& view);

} // namespace toki::viewport
