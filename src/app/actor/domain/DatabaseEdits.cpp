#include "DatabaseEdits.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/Derivation.h"
#include "app/actor/domain/ParamReading.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <expected>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

std::expected<void, std::string> addVariantForParams(ActorDatabase& database,
                                                     const ActorEntry& current,
                                                     const PlacementValues& placement) {
    if (current.params.empty()) {
        return std::unexpected(
            std::format("{} names no params, so a variant cannot tell these params from others",
                        current.actor));
    }
    ActorEntry& entry = database.entries.try_emplace(current.actor, current).first->second;
    const ActorVariant* matched = findVariant(entry, placement);
    ActorVariant variant = matched != nullptr ? *matched : ActorVariant{};
    std::vector<ParamMatch> match = makeParamsMatch(entry, placement);
    for (const ParamMatch& kept : variant.match) {
        if (isBuiltInParam(kept.param)) {
            match.push_back(kept);
        }
    }
    variant.match = std::move(match);
    variant.defaultParams = placement.params;
    variant.location = {};
    entry.variants.insert(entry.variants.begin(), std::move(variant));
    return {};
}

std::expected<void, std::string> setEntry(ActorDatabase& database, ActorEntry entry) {
    if (entry.actor.empty()) {
        return std::unexpected(std::string("an entry needs the enum name of its actor"));
    }
    std::string actor = entry.actor;
    database.entries.insert_or_assign(std::move(actor), std::move(entry));
    return {};
}

std::expected<void, std::string> removeEntry(ActorDatabase& database, const std::string& actor) {
    if (database.entries.erase(actor) == 0) {
        return std::unexpected(
            std::format("the actor database has no entry for {} to remove", actor));
    }
    return {};
}

std::expected<void, std::string> deriveEntries(ActorDatabase& database,
                                               const hackeroot::Workspace& workspace,
                                               const ActorDatabase& builtIn,
                                               const std::vector<std::string>& actors) {
    const std::vector<hackeroot::ActorEntry>& table = workspace.getTables().actors;
    for (const std::string& name : actors) {
        const auto actor = std::ranges::find(table, name, &hackeroot::ActorEntry::enumName);
        if (actor == table.end() || actor->isUnset) {
            return std::unexpected(std::format("the actor table has no {} to derive", name));
        }
        database.entries.insert_or_assign(name, deriveEntry(workspace, builtIn, *actor));
    }
    return {};
}

} // namespace toki::actor
