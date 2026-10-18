#pragma once

#include "app/viewport/Types.h"

namespace toki::viewport {

/// What a click in the viewport selects: the nodes of the scene's tree, such
/// as rooms and placed actors, or the faces, edges or vertices of what is
/// drawn. Only nodes can be selected yet.
enum class SelectionMode : u8 {
    Node,
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
    SelectionMode mode = SelectionMode::Node;
    EditTool tool = EditTool::Select;
};

} // namespace toki::viewport
