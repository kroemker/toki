#pragma once

#include "app/viewport/Types.h"

namespace toki::viewport {

/// What a click in the viewport selects: whole placements, or the faces,
/// edges or vertices of what is drawn. Only placements can be selected yet.
enum class SelectionMode : u8 {
    Object,
    Face,
    Edge,
    Vertex,
};

/// What dragging in the viewport does to what is selected. Only moving is
/// built yet.
enum class EditTool : u8 {
    Select,
    Move,
    Rotate,
    Scale,
};

/// How the viewport selects and edits.
struct ViewportTools {
    SelectionMode mode = SelectionMode::Object;
    EditTool tool = EditTool::Move;
};

} // namespace toki::viewport
