#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/domain/SymbolCatalog.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/core/ui/PropertyTable.h"
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
    FindCollection findCollection;
};

[[nodiscard]] bool editText(const char* label, std::string& text);

[[nodiscard]] bool editNotes(const char* label, std::string& text);

/// A number that is left out unless its box is ticked, typed in; `shown`
/// while it is left out.
[[nodiscard]] bool editOptionalNumber(const char* label, std::optional<f32>& value, f32 shown);

[[nodiscard]] bool editVector(const char* label, std::array<f32, 3>& value);

/// A colour that is left out unless its box is ticked.
[[nodiscard]] bool editOptionalColor(const char* label, std::optional<std::array<u8, 4>>& color);

/// One of the values `names` gives an enum, shown by its label.
template <typename Enum, usize Count>
[[nodiscard]] bool editName(const char* label, Enum& value,
                            const std::array<EntryName<Enum>, Count>& names) {
    core::drawPropertyLabel(label);
    bool isChanged = false;
    ImGui::PushID(label);
    if (ImGui::BeginCombo("##value", std::string(getEntryLabel(names, value)).c_str())) {
        for (const EntryName<Enum>& name : names) {
            if (ImGui::Selectable(std::string(name.label).c_str(), name.value == value)) {
                isChanged = value != name.value;
                value = name.value;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return isChanged;
}

/// One of the values `names` gives an enum, or `unset` to leave it out.
template <typename Enum, usize Count>
[[nodiscard]] bool editOptionalName(const char* label, std::optional<Enum>& value,
                                    const std::array<EntryName<Enum>, Count>& names,
                                    const char* unset) {
    core::drawPropertyLabel(label);
    bool isChanged = false;
    ImGui::PushID(label);
    const std::string shown =
        value ? std::string(getEntryLabel(names, *value)) : std::string(unset);
    if (ImGui::BeginCombo("##value", shown.c_str())) {
        if (ImGui::Selectable(unset, !value)) {
            isChanged = value.has_value();
            value.reset();
        }
        for (const EntryName<Enum>& name : names) {
            if (ImGui::Selectable(std::string(name.label).c_str(), value == name.value)) {
                isChanged = value != name.value;
                value = name.value;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
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

/// The collection of `names` a choice takes its values from, or none, shown as
/// "Listed here", for values the entry lists itself.
[[nodiscard]] bool editCollection(const char* label, std::optional<std::string>& collection,
                                  std::span<const std::string> names);

/// A number that may be negative, typed in hexadecimal.
[[nodiscard]] bool editSignedHex(const char* label, s64& value);

/// An actor of the actor table by its enum name.
[[nodiscard]] bool editActor(const char* label, std::string& actor, const EditContext& context);

/// Params, or any 16 bits, in hexadecimal.
[[nodiscard]] bool editHex(const char* label, u16& value);

/// A small button with an icon and a tooltip.
[[nodiscard]] bool drawIconButton(const char* icon, const char* tooltip);

} // namespace toki::actor
