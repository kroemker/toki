#include "Hotkeys.h"

#include <imgui.h>

#include <array>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace toki::core {

namespace {

/// Keys whose Dear ImGui name is a word for what the key prints.
constexpr auto PrintedKeys = std::to_array<std::pair<std::string_view, std::string_view>>({
    {"Comma", ","},
    {"Period", "."},
    {"Minus", "-"},
    {"Equal", "="},
    {"Slash", "/"},
    {"Semicolon", ";"},
    {"Apostrophe", "'"},
    {"LeftBracket", "["},
    {"RightBracket", "]"},
    {"Backslash", "\\"},
    {"GraveAccent", "`"},
});

KeyBindings& getBindings() {
    static KeyBindings bindings;
    return bindings;
}

[[nodiscard]] ImGuiKey findKey(const std::string& name) {
    static const std::map<std::string, ImGuiKey, std::less<>> keys = [] {
        std::map<std::string, ImGuiKey, std::less<>> named;
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
            named.emplace(ImGui::GetKeyName(static_cast<ImGuiKey>(key)),
                          static_cast<ImGuiKey>(key));
        }
        return named;
    }();
    const auto found = keys.find(name);
    return found == keys.end() ? ImGuiKey_None : found->second;
}

[[nodiscard]] ImGuiKeyChord makeChord(const KeyChord& chord) {
    const ImGuiKey key = findKey(chord.key);
    if (key == ImGuiKey_None) {
        return ImGuiKey_None;
    }
    ImGuiKeyChord made = key;
    if (chord.ctrl) {
        made |= ImGuiMod_Ctrl;
    }
    if (chord.shift) {
        made |= ImGuiMod_Shift;
    }
    if (chord.alt) {
        made |= ImGuiMod_Alt;
    }
    return made;
}

} // namespace

void setKeyBindings(const KeyBindings& bindings) {
    getBindings() = bindings;
}

const KeyBindings& getKeyBindings() {
    return getBindings();
}

bool isActionPressed(const Action action) {
    const ImGuiKeyChord chord = makeChord(getBindings().getChord(action));
    if (chord == ImGuiKey_None) {
        return false;
    }
    if (getActionInfo(action).scope == ActionScope::Global) {
        return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal);
    }
    return !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(chord);
}

std::string getShortcutLabel(const Action action) {
    KeyChord chord = getBindings().getChord(action);
    for (const auto& [name, printed] : PrintedKeys) {
        if (chord.key == name) {
            chord.key = printed;
        }
    }
    return formatKeyChord(chord);
}

std::string makeShortcutText(const std::string_view text, const Action action) {
    const std::string label = getShortcutLabel(action);
    return label.empty() ? std::string(text) : std::format("{} ({})", text, label);
}

} // namespace toki::core
