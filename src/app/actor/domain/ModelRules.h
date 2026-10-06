#pragma once

#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/OverlayCode.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace toki::actor {

/// The object folder an object's enum name stands for (`OBJECT_ANI` is
/// `object_ani`), from the checkout's object table.
using FindObjectFolder = std::function<std::optional<std::string>(std::string_view)>;

/// Reads how an actor is drawn out of its overlay, by the calls its profile's
/// `Init` and `Draw` make, and those of the helpers they call directly. A
/// profile without a `Draw` draws with the first one the overlay assigns to
/// `actor.draw` anywhere, as actors that wait for an object to load do.
///
/// - the skeleton of the first `SkelAnime_Init`, `SkelAnime_InitFlex` or
///   `SkelAnime_InitLink` that names one, flexible if its header declares a
///   `FlexSkeletonHeader`, and the animation that init names, or else the first
///   `Animation_*` call that names an `AnimationHeader`. An actor whose `Init`
///   sets up no skeleton is searched whole, since one that waits for an object
///   sets its skeleton up once the object has loaded;
/// - without a skeleton, every display list `Draw` passes to
///   `Gfx_DrawDListOpa`, `Gfx_DrawDListXlu`, or `gSPDisplayList` on
///   `POLY_OPA_DISP` or `POLY_XLU_DISP`, branches and all;
/// - the scale of the last `Actor_SetScale` with a constant, or else of the
///   `ICHAIN_VEC3F_DIV1000(scale, ...)` in the init chain `Init` processes;
/// - the height offset `ActorShape_Init` is given.
///
/// Only literals count: what a symbol is depends on nothing but its name. What
/// is not a literal is a gap saying where and why.
[[nodiscard]] ActorModel applyModelRules(std::string_view actorName, const OverlayCode& code,
                                         const FindObjectFolder& findObjectFolder);

} // namespace toki::actor
