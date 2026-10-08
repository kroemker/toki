#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/scene/Types.h"
#include "app/scene/domain/ActorObjects.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace toki::scene {

/// The ID the checkout's actor table gives the actor of an enum name.
using FindActorId = std::function<std::optional<s16>(std::string_view)>;

/// What `variant` of `entry` draws, found in `objects`: its skeleton posed on
/// its frame, or its display lists, each placed, coloured and with its
/// segments; its scale, height offset and the axes it turns by; its shadow and
/// its children. A variant takes what it leaves out from its entry. Whatever
/// the objects do not have is a diagnostic, and is left out.
[[nodiscard]] ActorDrawing makeActorDrawing(const actor::ActorEntry& entry,
                                            const actor::ActorVariant& variant,
                                            ActorObjects& objects, const FindActorId& findActorId,
                                            std::vector<base::Diagnostic>& diagnostics);

} // namespace toki::scene
