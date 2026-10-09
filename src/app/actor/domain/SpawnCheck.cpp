#include "SpawnCheck.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/domain/FieldReading.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>

namespace toki::actor {

SpawnCheck checkSpawn(const ActorEntry& entry, const PlacementValues& placement,
                      const std::optional<TimeOfDay> time) {
    const ActorVariant* variant = findVariant(entry, placement);
    const std::optional<SpawnCondition>& spawn =
        variant != nullptr && variant->spawn ? variant->spawn : entry.spawn;
    SpawnCheck check;
    if (!spawn) {
        return check;
    }
    if (spawn->age) {
        check.needs.push_back(std::format("{} only", getEntryName(AgeNames, *spawn->age)));
        check.isSpawned = !placement.age || *placement.age == *spawn->age;
    }
    if (spawn->time) {
        check.needs.push_back(std::format("{} only", getEntryName(TimeNames, *spawn->time)));
        check.isSpawned = check.isSpawned && (!time || *time == *spawn->time);
    }
    if (spawn->flagField) {
        const auto field = std::ranges::find(entry.fields, *spawn->flagField, &ParamField::name);
        const std::string value =
            field != entry.fields.end()
                ? std::format(" {}", makeFieldText(*field, readFieldValue(*field, placement)))
                : std::string();
        check.needs.push_back(
            std::format("{}{} {}", *spawn->flagField, value, spawn->isFlagSet ? "set" : "unset"));
    }
    return check;
}

} // namespace toki::actor
