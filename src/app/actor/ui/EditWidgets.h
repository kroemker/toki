#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/object/domain/ObjectAssets.h"

#include <imgui.h>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace toki::actor {

/// What the entry editor's widgets pick from: the checkout's objects, their
/// symbols, and the actors of the actor table.
struct EditContext {
    SymbolCatalog& symbolCatalog;
    std::span<const std::string> objects;
    std::span<const std::string> actors;
};

[[nodiscard]] bool editText(const char* label, std::string& text);

[[nodiscard]] bool editNotes(const char* label, std::string& text);

/// Text that is left out when empty.
[[nodiscard]] bool editOptionalText(const char* label, std::optional<std::string>& text);

/// A number that is left out unless its box is ticked; `shown` while it is.
[[nodiscard]] bool editOptionalNumber(const char* label, std::optional<f32>& value, f32 shown);

[[nodiscard]] bool editVector(const char* label, std::array<f32, 3>& value);

/// A colour that is left out unless its box is ticked.
[[nodiscard]] bool editOptionalColor(const char* label, std::optional<std::array<u8, 4>>& color);

/// One of the names `names` gives an enum.
template <typename Enum, usize Count>
[[nodiscard]] bool editName(const char* label, Enum& value,
                            const std::array<EntryName<Enum>, Count>& names) {
    bool isChanged = false;
    if (ImGui::BeginCombo(label, std::string(getEntryName(names, value)).c_str())) {
        for (const EntryName<Enum>& name : names) {
            if (ImGui::Selectable(std::string(name.name).c_str(), name.value == value)) {
                isChanged = value != name.value;
                value = name.value;
            }
        }
        ImGui::EndCombo();
    }
    return isChanged;
}

/// One of the names `names` gives an enum, or `inherited` to leave it out.
template <typename Enum, usize Count>
[[nodiscard]] bool editOptionalName(const char* label, std::optional<Enum>& value,
                                    const std::array<EntryName<Enum>, Count>& names,
                                    const char* inherited) {
    bool isChanged = false;
    const std::string shown =
        value ? std::string(getEntryName(names, *value)) : std::string(inherited);
    if (ImGui::BeginCombo(label, shown.c_str())) {
        if (ImGui::Selectable(inherited, !value)) {
            isChanged = value.has_value();
            value.reset();
        }
        for (const EntryName<Enum>& name : names) {
            if (ImGui::Selectable(std::string(name.name).c_str(), value == name.value)) {
                isChanged = value != name.value;
                value = name.value;
            }
        }
        ImGui::EndCombo();
    }
    return isChanged;
}

/// An object of the checkout, or none to take the one it inherits, named by
/// `inherited`.
[[nodiscard]] bool editObject(const char* label, std::optional<std::string>& object,
                              const EditContext& context, std::string_view inherited);

/// A symbol of `object` of the kinds `kinds`, picked from its catalog once
/// read, or typed.
[[nodiscard]] bool editSymbol(const char* label, std::string& symbol,
                              const std::optional<std::string>& object,
                              std::span<const object::AssetKind> kinds, const EditContext& context);

/// A symbol that is left out when empty.
[[nodiscard]] bool editOptionalSymbol(const char* label, std::optional<std::string>& symbol,
                                      const std::optional<std::string>& object,
                                      std::span<const object::AssetKind> kinds,
                                      const EditContext& context);

/// An actor of the actor table by its enum name.
[[nodiscard]] bool editActor(const char* label, std::string& actor, const EditContext& context);

/// Params, or any 16 bits, in hexadecimal.
[[nodiscard]] bool editHex(const char* label, u16& value);

/// A small button with an icon and a tooltip.
[[nodiscard]] bool drawIconButton(const char* icon, const char* tooltip);

} // namespace toki::actor
