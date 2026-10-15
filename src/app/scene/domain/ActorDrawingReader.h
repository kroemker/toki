#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ItemModels.h"
#include "app/actor/domain/ValueCollection.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorObjects.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"

#include <glm/ext/matrix_float4x4.hpp>

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace toki::scene {

/// What item parts are drawn from: the checkout's get-item models, and the
/// tables of the actor's own an item may be picked from.
struct ItemSource {
    const actor::ItemModels* models = nullptr;
    std::span<const actor::ValueCollection> tables;
};

/// The ID the checkout's actor table gives the actor of an enum name.
using FindActorId = std::function<std::optional<s16>(std::string_view)>;

/// The entry of the actor of an enum name, for the params its children take.
using FindEntry = std::function<const actor::ActorEntry*(std::string_view)>;

/// The matrix of `placement`: moved, turned about Z, then Y, then X, and
/// scaled.
[[nodiscard]] glm::mat4 makePlacementMatrix(const actor::PartPlacement& placement);

/// What `variant` of `entry` draws, found in `objects`: each of its parts, a
/// display list or a skeleton posed on the first frame of its pose, under what
/// the part says it is drawn with; its scale, height offset and the axes it
/// turns by; and its children. An item is drawn as its model in `items` says,
/// from the object the model names unless the part names its own; one picked
/// by a param is drawn for each value it can pick. A variant takes what it
/// leaves out from its entry. Whatever the objects do not have is a
/// diagnostic, and is left out.
[[nodiscard]] ActorDrawing makeActorDrawing(const actor::ActorEntry& entry,
                                            const actor::ActorVariant& variant,
                                            ActorObjects& objects, const FindActorId& findActorId,
                                            const FindEntry& findEntry, const ItemSource& items,
                                            std::vector<base::Diagnostic>& diagnostics);

/// What `adjustment` of `entry` changes, its segments' symbols found in the
/// entry's object among `objects`; what is not there is a diagnostic.
[[nodiscard]] AdjustmentDrawing makeAdjustmentDrawing(const actor::ActorEntry& entry,
                                                      const actor::ActorAdjustment& adjustment,
                                                      ActorObjects& objects,
                                                      std::vector<base::Diagnostic>& diagnostics);

} // namespace toki::scene
