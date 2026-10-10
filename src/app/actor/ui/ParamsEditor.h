#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"

#include <optional>
#include <string>

namespace toki::actor {

/// Edits an entry's params: a strip of 16 bits for each source in use, each
/// param in its own colour, where clicking a bit gives it to the param being
/// edited or takes it back, and bits another param has are not clickable;
/// then the params, and the one being edited: its name, kind, source,
/// description, sign and hexadecimal options, and a choice's values. Renaming
/// a param renames it in every match; a name that is taken or built in is
/// shown, not applied. Returns whether anything changed.
class ParamsEditor {
public:
    [[nodiscard]] bool draw(ActorEntry& entry);

private:
    [[nodiscard]] std::optional<usize> findSelected(const ActorEntry& entry) const;
    void select(const ActorEntry& entry, std::optional<usize> index);
    [[nodiscard]] bool drawStrip(ActorEntry& entry, ParamSource source);
    [[nodiscard]] bool drawBit(ActorEntry& entry, ParamSource source, u8 bit);
    [[nodiscard]] bool drawList(ActorEntry& entry);
    [[nodiscard]] bool drawParam(ActorEntry& entry, ActorParam& param);
    [[nodiscard]] static bool drawChoices(ActorParam& param);

    std::string editedActor;
    std::optional<std::string> selected;
    std::string nameText;
    bool isNameEdited = false;
};

} // namespace toki::actor
