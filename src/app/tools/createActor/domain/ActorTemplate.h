#pragma once

#include "app/tools/createActor/Types.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/ConfigDefines.h"
#include "hackeroot/Tables.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace toki::createActor {

/// The documented names a new actor is made of, as the checkout writes them:
/// its flags, `ACTOR_FLAG_` in `actor.h`, and how its overlay is loaded,
/// `ACTOROVL_ALLOC_` in `z_actor_dlftbls.h`.
struct ActorChoices {
    std::vector<hackeroot::DocumentedDefine> flags;
    std::vector<hackeroot::DocumentedDefine> allocations;
};

/// Reads the names a new actor may choose from, out of the checkout at
/// `repository`.
[[nodiscard]] std::expected<ActorChoices, std::string>
readActorChoices(const std::filesystem::path& repository);

/// A new actor, as it is to be written: its name as the actor table spells it,
/// `En_Foo`, a line on what it is, how its overlay is loaded and whether it
/// takes an unused slot of the actor table, keeping every ID after it; its
/// category and flags; the object it uses by its table entry's name, or a new
/// one the object table gains; whether it draws, has the includes most actors
/// have, and is a `DynaPolyActor` whose collision moves and turns with it.
struct ActorDraft {
    std::string name;
    std::string description;
    std::string allocation = "ACTOROVL_ALLOC_NORMAL";
    bool takesUnsetSlot = true;
    std::string category = "ACTORCAT_MISC";
    std::vector<std::string> flags;
    std::string object = "gameplay_keep";
    std::optional<std::string> newObject;
    bool draws = true;
    bool includesCommon = false;
    bool isDynaPoly = false;
    bool movesCollision = true;
    bool turnsCollision = false;
};

/// What is wrong with `draft` as a new actor of the checkout whose tables are
/// `tables`, or nothing: a name that is no C identifier, an actor the table has
/// already, an object it does not have, a description that would end the
/// comment it is written in, collision that neither moves nor turns.
[[nodiscard]] std::optional<std::string> findDraftProblem(const ActorDraft& draft,
                                                          const hackeroot::Tables& tables);

/// The overlay's header and main file for `draft`, from the template the
/// decomp's actors follow: the actor's struct with an action function, its
/// profile, and `Init`, `Destroy`, `Update` and `Draw` to fill in.
[[nodiscard]] std::string makeActorHeader(const ActorDraft& draft);
[[nodiscard]] std::string makeActorSource(const ActorDraft& draft, const hackeroot::Tables& tables);

/// What creating `draft` does to the checkout at `repository`: its entry in the
/// actor table, the object table's for a new object, their segments in
/// `spec/spec`, and the overlay's two files in `src/overlays/actors/ovl_<name>/`.
[[nodiscard]] std::expected<hackeroot::EditPlan, std::string>
makeActorPlan(const std::filesystem::path& repository, const ActorDraft& draft,
              const hackeroot::Tables& tables);

} // namespace toki::createActor
