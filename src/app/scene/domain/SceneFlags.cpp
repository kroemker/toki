#include "SceneFlags.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/scene/Types.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneHeader.h"

#include <algorithm>
#include <array>
#include <optional>
#include <tuple>
#include <vector>

namespace toki::scene {

namespace {

constexpr s64 FirstTemporaryFlag = 0x20;

void addUsers(std::vector<SceneFlagUse>& flags, const SceneActors& actors, const s16 actorId,
              const std::optional<usize> room, const std::array<s16, 3>& position,
              const actor::PlacementValues& placement) {
    const auto model = actors.models.find(actorId);
    if (model == actors.models.end() || !model->second.entry) {
        return;
    }
    for (const actor::ActorParam& param : model->second.entry->params) {
        if (!param.sceneFlag) {
            continue;
        }
        const std::optional<s64> number = findFlagNumber(param, placement);
        if (!number) {
            continue;
        }
        auto flag = std::ranges::find_if(flags, [&](const SceneFlagUse& known) {
            return known.kind == *param.sceneFlag && known.number == *number;
        });
        if (flag == flags.end()) {
            flags.push_back(SceneFlagUse{.kind = *param.sceneFlag, .number = *number});
            flag = flags.end() - 1;
        }
        flag->users.push_back(FlagUser{.actorId = actorId,
                                       .room = room,
                                       .position = position,
                                       .param = param.name,
                                       .use = param.flagUse});
    }
}

} // namespace

bool SceneFlagUse::isSet() const {
    return std::ranges::any_of(
        users, [](const FlagUser& user) { return user.use != actor::FlagUse::Reads; });
}

bool SceneFlagUse::isRead() const {
    return std::ranges::any_of(
        users, [](const FlagUser& user) { return user.use != actor::FlagUse::Sets; });
}

s64 getSceneFlagCount(const actor::SceneFlag kind) {
    return kind == actor::SceneFlag::Switch || kind == actor::SceneFlag::Collectible ? 0x40 : 0x20;
}

bool isSavedSceneFlag(const actor::SceneFlag kind, const s64 number) {
    return (kind != actor::SceneFlag::Switch && kind != actor::SceneFlag::Collectible) ||
           number < FirstTemporaryFlag;
}

std::optional<s64> findFlagNumber(const actor::ActorParam& param,
                                  const actor::PlacementValues& placement) {
    if (!param.sceneFlag || param.mask == 0) {
        return std::nullopt;
    }
    const s64 value = actor::readParamValue(param, placement);
    const u8 width = actor::findMaskWidth(param.mask);
    const s64 highest = param.isSigned ? -1 : (s64{1} << width) - 1;
    if (value == highest || value < 0 || value >= getSceneFlagCount(*param.sceneFlag)) {
        return std::nullopt;
    }
    return value;
}

std::vector<SceneFlagUse> listSceneFlags(const SceneLayer& layer, const SceneActors& actors) {
    std::vector<SceneFlagUse> flags;
    const std::optional<actor::Age> age = findLayerAge(layer.index);
    for (usize room = 0; room < layer.rooms.size(); ++room) {
        for (const ActorPlacement& placed : layer.rooms[room].actors) {
            addUsers(
                flags, actors, placed.id, room, placed.position,
                makePlacementValues(actors, static_cast<u16>(placed.params), placed.rotation, age));
        }
    }
    for (const TransitionActorPlacement& placed : layer.header.transitionActors) {
        addUsers(flags, actors, placed.id, std::nullopt, placed.position,
                 makePlacementValues(actors, static_cast<u16>(placed.params),
                                     {0, placed.rotationY, 0}, age));
    }
    std::ranges::stable_sort(flags, {}, [](const SceneFlagUse& flag) {
        return std::tuple(flag.users.size() < 2, flag.kind, flag.number);
    });
    return flags;
}

} // namespace toki::scene
