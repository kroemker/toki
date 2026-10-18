#pragma once

#include "app/core/Types.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::core {

/// Something a key can be bound to.
enum class Action : u8 {
    SaveScene,
    OpenSettings,
    Build,
    Run,
    Undo,
    Redo,
    ToggleXray,
    ToggleOverlays,
    ShadingN64,
    ShadingShadeOnly,
    ShadingTextureOnly,
    ShadingNormals,
    ShadingDepth,
    ShadingWireframe,
    NodeMode,
    FaceMode,
    EdgeMode,
    VertexMode,
    SelectTool,
    MoveTool,
    RotateTool,
    ScaleTool,
    Grab,
    Turn,
    ConstrainX,
    ConstrainY,
    ConstrainZ,
    ConstrainPlaneX,
    ConstrainPlaneY,
    ConstrainPlaneZ,
};

inline constexpr usize ActionCount = static_cast<usize>(Action::ConstrainPlaneZ) + 1;

/// Where an action's key is heard: anywhere in the window, or only while the
/// cursor is over the viewport.
enum class ActionScope : u8 {
    Global,
    Viewport,
};

/// An action as the config and the settings name it, and the key it has
/// until the person binds another.
struct ActionInfo {
    Action action = Action::SaveScene;
    std::string_view id;
    std::string_view label;
    ActionScope scope = ActionScope::Global;
    std::string_view defaultChord;
};

/// Every action, in the order the settings list them.
[[nodiscard]] std::span<const ActionInfo> getActions();

[[nodiscard]] const ActionInfo& getActionInfo(Action action);

/// The action the config calls `id`, if any.
[[nodiscard]] std::optional<Action> findAction(std::string_view id);

/// A key with the modifiers held for it, its key named as Dear ImGui names
/// keys: `S`, `F5`, `Comma`, `1`. An empty key binds nothing.
struct KeyChord {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    std::string key;

    [[nodiscard]] bool isBound() const {
        return !key.empty();
    }

    friend bool operator==(const KeyChord&, const KeyChord&) = default;
};

/// A chord written as the config writes one, `Ctrl+Shift+Z`, modifiers in
/// any order and case; empty for none. Nothing when it names no key, or a
/// modifier twice.
[[nodiscard]] std::optional<KeyChord> parseKeyChord(std::string_view text);

/// `chord` as the config writes it: Ctrl, Shift and Alt in that order, then
/// the key.
[[nodiscard]] std::string formatKeyChord(const KeyChord& chord);

/// The key each action is bound to.
class KeyBindings {
public:
    /// Every action bound to its default.
    KeyBindings();

    [[nodiscard]] const KeyChord& getChord(Action action) const {
        return chords[static_cast<usize>(action)];
    }

    void setChord(Action action, KeyChord chord) {
        chords[static_cast<usize>(action)] = std::move(chord);
    }

    [[nodiscard]] bool isDefault(Action action) const;

    /// The pairs of actions bound to the same chord where both can be heard
    /// at once: any two of the same scope, and a global one with any other.
    [[nodiscard]] std::vector<std::pair<Action, Action>> findConflicts() const;

    friend bool operator==(const KeyBindings&, const KeyBindings&) = default;

private:
    std::array<KeyChord, ActionCount> chords;
};

} // namespace toki::core
