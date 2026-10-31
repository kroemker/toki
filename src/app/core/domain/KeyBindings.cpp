#include "KeyBindings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::core {

namespace {

constexpr auto Actions = std::to_array<ActionInfo>({
    {Action::SaveScene, "save_scene", "Save Scene", ActionScope::Global, "Ctrl+S"},
    {Action::OpenSettings, "open_settings", "Open Settings", ActionScope::Global, "Ctrl+Comma"},
    {Action::Build, "build", "Build", ActionScope::Global, "Ctrl+B"},
    {Action::Run, "run", "Build and Run", ActionScope::Global, "F5"},
    {Action::Undo, "undo", "Undo", ActionScope::Global, "Ctrl+Z"},
    {Action::Redo, "redo", "Redo", ActionScope::Global, "Ctrl+Shift+Z"},
    {Action::ToggleXray, "toggle_xray", "Toggle X-Ray", ActionScope::Viewport, "Alt+Z"},
    {Action::ToggleOverlays, "toggle_overlays", "Toggle Overlays", ActionScope::Viewport,
     "Shift+Alt+Z"},
    {Action::ShadingN64, "shading_n64", "N64 Shading", ActionScope::Viewport, "Alt+1"},
    {Action::ShadingShadeOnly, "shading_shade_only", "Shade Only", ActionScope::Viewport, "Alt+2"},
    {Action::ShadingTextureOnly, "shading_texture_only", "Texture Only", ActionScope::Viewport,
     "Alt+3"},
    {Action::ShadingNormals, "shading_normals", "Normals", ActionScope::Viewport, "Alt+4"},
    {Action::ShadingDepth, "shading_depth", "Depth", ActionScope::Viewport, "Alt+5"},
    {Action::ShadingWireframe, "shading_wireframe", "Wireframe", ActionScope::Viewport, "Alt+6"},
    {Action::NodeMode, "node_mode", "Node Mode", ActionScope::Viewport, "4"},
    {Action::FaceMode, "face_mode", "Face Mode", ActionScope::Viewport, "3"},
    {Action::EdgeMode, "edge_mode", "Edge Mode", ActionScope::Viewport, "2"},
    {Action::VertexMode, "vertex_mode", "Vertex Mode", ActionScope::Viewport, "1"},
    {Action::ToggleEditMode, "toggle_edit_mode", "Toggle Node and Last Edit Mode",
     ActionScope::Viewport, "Tab"},
    {Action::SelectTool, "select_tool", "Select Tool", ActionScope::Viewport, "W"},
    {Action::MoveTool, "move_tool", "Move Tool", ActionScope::Viewport, ""},
    {Action::RotateTool, "rotate_tool", "Rotate Tool", ActionScope::Viewport, ""},
    {Action::ScaleTool, "scale_tool", "Scale Tool", ActionScope::Viewport, ""},
    {Action::Grab, "grab", "Move with the Mouse", ActionScope::Viewport, "G"},
    {Action::Turn, "turn", "Rotate with the Mouse", ActionScope::Viewport, "R"},
    {Action::Stretch, "stretch", "Scale with the Mouse", ActionScope::Viewport, "S"},
    {Action::ConstrainX, "constrain_x", "Hold to X", ActionScope::Transform, "X"},
    {Action::ConstrainY, "constrain_y", "Hold to Y", ActionScope::Transform, "Y"},
    {Action::ConstrainZ, "constrain_z", "Hold to Z", ActionScope::Transform, "Z"},
    {Action::ConstrainPlaneX, "constrain_plane_x", "Hold to Y and Z", ActionScope::Transform,
     "Shift+X"},
    {Action::ConstrainPlaneY, "constrain_plane_y", "Hold to X and Z", ActionScope::Transform,
     "Shift+Y"},
    {Action::ConstrainPlaneZ, "constrain_plane_z", "Hold to X and Y", ActionScope::Transform,
     "Shift+Z"},
    {Action::DeleteComponents, "delete_components", "Delete", ActionScope::Viewport, "Delete"},
    {Action::MergeVertices, "merge_vertices", "Merge at Center", ActionScope::Viewport, "M"},
    {Action::ExtrudeFaces, "extrude_faces", "Extrude Faces", ActionScope::Viewport, "E"},
    {Action::InsetFaces, "inset_faces", "Inset Faces", ActionScope::Viewport, "I"},
    {Action::AddBox, "add_box", "Add Box", ActionScope::Viewport, "Shift+B"},
    {Action::AddPlane, "add_plane", "Add Plane", ActionScope::Viewport, "Shift+P"},
    {Action::DeleteComponentsAgain, "delete_components_again", "Delete, Second Key",
     ActionScope::Viewport, "X"},
    {Action::FillVertices, "fill_vertices", "Make Face", ActionScope::Viewport, "F"},
    {Action::SplitFaces, "split_faces", "Split Faces", ActionScope::Viewport, "Y"},
    {Action::FlipFaces, "flip_faces", "Flip Faces", ActionScope::Viewport, "Alt+N"},
    {Action::SubdivideFaces, "subdivide_faces", "Subdivide", ActionScope::Viewport, "Alt+S"},
    {Action::Duplicate, "duplicate", "Duplicate", ActionScope::Viewport, "Shift+D"},
    {Action::Copy, "copy", "Copy", ActionScope::Viewport, "Ctrl+C"},
    {Action::Paste, "paste", "Paste", ActionScope::Viewport, "Ctrl+V"},
    {Action::SelectAll, "select_all", "Select All", ActionScope::Viewport, "A"},
    {Action::SelectNone, "select_none", "Select None", ActionScope::Viewport, "Alt+A"},
    {Action::InvertSelection, "invert_selection", "Invert Selection", ActionScope::Viewport,
     "Ctrl+I"},
});

static_assert(Actions.size() == ActionCount, "every action needs its info");

[[nodiscard]] bool isSameWord(const std::string_view first, const std::string_view second) {
    return std::ranges::equal(first, second, [](const char a, const char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}

[[nodiscard]] bool isHeardTogether(const ActionScope first, const ActionScope second) {
    return first == second || first == ActionScope::Global || second == ActionScope::Global;
}

} // namespace

std::span<const ActionInfo> getActions() {
    return Actions;
}

const ActionInfo& getActionInfo(const Action action) {
    return Actions[static_cast<usize>(action)];
}

std::optional<Action> findAction(const std::string_view id) {
    const auto found = std::ranges::find(Actions, id, &ActionInfo::id);
    return found == Actions.end() ? std::nullopt : std::optional(found->action);
}

std::optional<KeyChord> parseKeyChord(const std::string_view text) {
    KeyChord chord;
    if (text.empty()) {
        return chord;
    }
    usize start = 0;
    while (true) {
        const usize plus = text.find('+', start);
        const std::string_view part = text.substr(start, plus - start);
        if (plus == std::string_view::npos) {
            if (part.empty()) {
                return std::nullopt;
            }
            chord.key = std::string(part);
            return chord;
        }
        bool* modifier = nullptr;
        if (isSameWord(part, "Ctrl")) {
            modifier = &chord.ctrl;
        } else if (isSameWord(part, "Shift")) {
            modifier = &chord.shift;
        } else if (isSameWord(part, "Alt")) {
            modifier = &chord.alt;
        }
        if (modifier == nullptr || *modifier) {
            return std::nullopt;
        }
        *modifier = true;
        start = plus + 1;
    }
}

std::string formatKeyChord(const KeyChord& chord) {
    if (!chord.isBound()) {
        return {};
    }
    std::string text;
    if (chord.ctrl) {
        text += "Ctrl+";
    }
    if (chord.shift) {
        text += "Shift+";
    }
    if (chord.alt) {
        text += "Alt+";
    }
    return text + chord.key;
}

KeyBindings::KeyBindings() {
    for (const ActionInfo& info : Actions) {
        chords[static_cast<usize>(info.action)] =
            parseKeyChord(info.defaultChord).value_or(KeyChord{});
    }
}

bool KeyBindings::isDefault(const Action action) const {
    return getChord(action) ==
           parseKeyChord(getActionInfo(action).defaultChord).value_or(KeyChord{});
}

std::vector<std::pair<Action, Action>> KeyBindings::findConflicts() const {
    std::vector<std::pair<Action, Action>> conflicts;
    for (usize first = 0; first < ActionCount; ++first) {
        for (usize second = first + 1; second < ActionCount; ++second) {
            if (chords[first].isBound() && chords[first] == chords[second] &&
                isHeardTogether(Actions[first].scope, Actions[second].scope)) {
                conflicts.emplace_back(Actions[first].action, Actions[second].action);
            }
        }
    }
    return conflicts;
}

} // namespace toki::core
