#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"

#include <optional>
#include <span>
#include <string>

namespace toki::actor {

/// Edits an entry's params: a strip of 16 bits for each source in use, each
/// param in its own colour, where clicking a bit gives it to the param being
/// edited or takes it back, and bits another param has are not clickable;
/// then the params, and the one being edited: its name, kind, source,
/// description, sign and hexadecimal options, and a choice's values, listed
/// here or taken from a collection of the checkout, after an offset. Renaming
/// a param renames it in every match; a name that is taken or built in is
/// shown, not applied. Returns whether anything changed.
class ParamsEditor {
public:
    /// `collections` are the names of the collections a choice of the entry
    /// can take its values from.
    [[nodiscard]] bool draw(ActorEntry& entry, std::span<const std::string> collections);

private:
    [[nodiscard]] std::optional<usize> findSelected(const ActorEntry& entry) const;
    void select(const ActorEntry& entry, std::optional<usize> index);
    [[nodiscard]] bool drawStrip(ActorEntry& entry, ParamSource source);
    [[nodiscard]] bool drawBit(ActorEntry& entry, ParamSource source, u8 bit);
    [[nodiscard]] bool drawList(ActorEntry& entry, std::span<const std::string> collections);
    [[nodiscard]] bool drawParam(ActorEntry& entry, ActorParam& param,
                                 std::span<const std::string> collections);
    [[nodiscard]] static bool drawChoices(ActorParam& param);

    std::string editedActor;
    std::optional<std::string> selected;
    std::string nameText;
    bool isNameEdited = false;
};

} // namespace toki::actor
