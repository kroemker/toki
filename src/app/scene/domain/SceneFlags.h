#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

/// A placed actor that uses a scene flag: where it is, in a room or between two
/// as a transition actor, the param that names the flag, and how it uses it.
struct FlagUser {
    s16 actorId = 0;
    std::optional<usize> room;
    std::array<s16, 3> position{};
    std::string param;
    actor::FlagUse use = actor::FlagUse::Reads;
};

/// One flag of a scene, by kind and number, and every placed actor of a layer
/// that uses it.
struct SceneFlagUse {
    actor::SceneFlag kind = actor::SceneFlag::Switch;
    s64 number = 0;
    std::vector<FlagUser> users;

    /// Whether an actor here sets it, and whether one reads it.
    [[nodiscard]] bool isSet() const;
    [[nodiscard]] bool isRead() const;
};

/// Two users of one flag, by their place in its list, the first setting what the
/// second reads, and whether the second sets what the first reads as well.
struct FlagLink {
    usize setter = 0;
    usize reader = 0;
    bool isMutual = false;
};

/// Every pair of `flag`'s users of which one sets the flag and the other reads
/// it, each pair once.
[[nodiscard]] std::vector<FlagLink> listFlagLinks(const SceneFlagUse& flag);

/// How many flags of `kind` a scene has: 64 switch and collectible flags, 32
/// treasure and clear flags.
[[nodiscard]] s64 getSceneFlagCount(actor::SceneFlag kind);

/// Whether the flag is kept with the save. Switch and collectible flags from
/// 0x20 are reset when the scene is left, as `Flags_GetSwitch` keeps them apart.
[[nodiscard]] bool isSavedSceneFlag(actor::SceneFlag kind, s64 number);

/// The scene flag `param` names for `placement`, or nothing when the param is
/// at its highest value, which actors take to mean no flag, or past the flags
/// the kind has.
[[nodiscard]] std::optional<s64> findFlagNumber(const actor::ActorParam& param,
                                                const actor::PlacementValues& placement);

/// Every scene flag the actors placed in `layer` use by their entries' params,
/// in its rooms and between them, the flags more than one actor uses first,
/// then by kind and number.
[[nodiscard]] std::vector<SceneFlagUse> listSceneFlags(const SceneLayer& layer,
                                                       const SceneActors& actors);

} // namespace toki::scene
