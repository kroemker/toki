#pragma once

#include "app/message/Types.h"
#include "app/message/domain/EditableBox.h"
#include "app/message/domain/Message.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/ui/BoxRenderer.h"

#include <imgui.h>

#include <span>
#include <vector>

namespace toki::message {

/// A code that draws nothing, a color or a pause, marked where it stands: the
/// unit it is in the box typed in, where the pen stood, and the color it
/// wrote in after.
struct BoxMarker {
    usize unit = 0;
    CaretPlace place;
    TextColor color;
    CodePiece code;
};

/// Where a marker is drawn and clicked: a head over the top of its line, a
/// swatch for a color and an icon for any other code, and a line down to the
/// line's foot.
struct MarkerShape {
    ImVec2 headMin;
    ImVec2 headMax;
    ImVec2 lineTop;
    ImVec2 lineBottom;

    [[nodiscard]] bool contains(const ImVec2 point) const {
        return point.x >= headMin.x && point.x <= headMax.x && point.y >= headMin.y &&
               point.y <= lineBottom.y;
    }
};

/// The markers of the codes of `editable` that draw nothing in `box`.
/// `sources` says where each byte was encoded from, in the box's own pieces.
[[nodiscard]] std::vector<BoxMarker> findMarkers(const EditableBox& editable, const LaidBox& box,
                                                 std::span<const TextPosition> sources);

[[nodiscard]] MarkerShape makeMarkerShape(const BoxMarker& marker, const LaidBox& box,
                                          const BoxImage& image);

/// The markers that stand at one place, drawn as one: the last of them, the
/// one whose color the text after them is drawn in, with how many there are.
struct MarkerGroup {
    std::vector<BoxMarker> markers;
    MarkerShape shape;
};

/// `markers`, in order, grouped where they stand at one place.
[[nodiscard]] std::vector<MarkerGroup> groupMarkers(std::span<const BoxMarker> markers,
                                                    const LaidBox& box, const BoxImage& image);

/// The group whose shape holds `point`, in screen pixels, if one does.
[[nodiscard]] const MarkerGroup* findGroupAt(std::span<const MarkerGroup> groups, ImVec2 point);

/// Draws `groups` over the box's image, and what the marker under the mouse
/// does as a tooltip, when it stands alone.
void drawMarkers(std::span<const MarkerGroup> groups, const BoxImage& image);

/// One row of a list of markers: its head and what it does, as an item. True
/// when it is clicked.
[[nodiscard]] bool drawMarkerRow(const BoxMarker& marker);

} // namespace toki::message
