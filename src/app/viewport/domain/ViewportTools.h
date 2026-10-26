#pragma once

#include "app/viewport/Types.h"

namespace toki::viewport {

/// What a click in the viewport selects: the nodes of the scene's tree, such
/// as rooms and placed actors, or the faces, edges or vertices of what is
/// edited, where the content's owner offers some (`ViewportChoices::editTargets`).
enum class SelectionMode : u8 {
    Node,
    Face,
    Edge,
    Vertex,
};

/// What dragging in the viewport does to what is selected.
enum class EditTool : u8 {
    Select,
    Move,
    Rotate,
    Scale,
};

/// How a pick changes the selection: it becomes what was picked, or what was
/// picked is added, taken away, or each added unless it was there, then taken
/// away.
enum class PickMode : u8 {
    Replace,
    Add,
    Remove,
    Toggle,
};

/// How the viewport selects and edits.
struct ViewportTools {
    SelectionMode mode = SelectionMode::Node;
    /// The mode of faces, edges or vertices last picked, which toggling out of
    /// node mode returns to.
    SelectionMode lastEditMode = SelectionMode::Face;
    EditTool tool = EditTool::Select;
    /// How a click or a box changes the selection while no key changes it.
    PickMode pickMode = PickMode::Replace;
};

} // namespace toki::viewport
