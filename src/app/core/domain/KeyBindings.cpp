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
    {Action::NodeMode, "node_mode", "Node Mode", ActionScope::Viewport, "1"},
    {Action::FaceMode, "face_mode", "Face Mode", ActionScope::Viewport, "2"},
    {Action::EdgeMode, "edge_mode", "Edge Mode", ActionScope::Viewport, "3"},
    {Action::VertexMode, "vertex_mode", "Vertex Mode", ActionScope::Viewport, "4"},
    {Action::ToggleEditMode, "toggle_edit_mode", "Toggle Node and Last Edit Mode",
     ActionScope::Viewport, "Tab"},
    {Action::SelectTool, "select_tool", "Select Tool", ActionScope::Viewport, "Q"},
    {Action::MoveTool, "move_tool", "Move Tool", ActionScope::Viewport, "W"},
    {Action::RotateTool, "rotate_tool", "Rotate Tool", ActionScope::Viewport, "E"},
    {Action::ScaleTool, "scale_tool", "Scale Tool", ActionScope::Viewport, ""},
    {Action::Grab, "grab", "Move with the Mouse", ActionScope::Viewport, "G"},
    {Action::Turn, "turn", "Rotate with the Mouse", ActionScope::Viewport, "R"},
    {Action::Stretch, "stretch", "Scale with the Mouse", ActionScope::Viewport, "S"},
    {Action::ConstrainX, "constrain_x", "Hold to X", ActionScope::Viewport, "X"},
    {Action::ConstrainY, "constrain_y", "Hold to Y", ActionScope::Viewport, "Y"},
    {Action::ConstrainZ, "constrain_z", "Hold to Z", ActionScope::Viewport, "Z"},
    {Action::ConstrainPlaneX, "constrain_plane_x", "Hold to Y and Z", ActionScope::Viewport,
     "Shift+X"},
    {Action::ConstrainPlaneY, "constrain_plane_y", "Hold to X and Z", ActionScope::Viewport,
     "Shift+Y"},
    {Action::ConstrainPlaneZ, "constrain_plane_z", "Hold to X and Y", ActionScope::Viewport,
     "Shift+Z"},
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
