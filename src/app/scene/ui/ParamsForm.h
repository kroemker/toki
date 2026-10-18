#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/scene/Types.h"

#include <string>

namespace toki::scene {

/// The params an actor's database entry names, drawn as rows of the property
/// table open: a flag as a checkbox, a number as a field, a choice as a
/// dropdown of its values. Each edits only its own bits of the placement.
class ParamsForm {
public:
    /// Draws a row for each param of `entry` as `placement` holds it, and one
    /// for bits of its params no param names, when any are set. True once an
    /// edit is to be made, with `placement` holding it.
    [[nodiscard]] bool draw(const actor::ActorEntry& entry, actor::PlacementValues& placement,
                            const actor::FindCollection& findCollection);

private:
    [[nodiscard]] bool drawParam(const actor::ActorParam& param, actor::PlacementValues& placement,
                                 const actor::ValueCollection* collection);
    [[nodiscard]] bool drawChoice(const actor::ActorParam& param, s64& value,
                                  const actor::ValueCollection* collection);

    std::string filter;
};

} // namespace toki::scene
