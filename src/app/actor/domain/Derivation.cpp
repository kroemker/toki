#include "Derivation.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModelReader.h"
#include "app/actor/domain/GuessedEntry.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace toki::actor {

ActorEntry deriveEntry(const hackeroot::Workspace& workspace, const ActorDatabase& builtIn,
                       const hackeroot::ActorEntry& actor) {
    if (const ActorEntry* known = findEntry(builtIn, actor.enumName)) {
        return *known;
    }
    return makeGuessedEntry(readActorModel(workspace, actor), actor.enumName);
}

std::vector<std::string> listNewActors(const hackeroot::Tables& tables,
                                       const ActorDatabase& database) {
    std::vector<std::string> actors;
    for (const hackeroot::ActorEntry& actor : tables.actors) {
        if (!actor.isUnset && !actor.enumName.empty() &&
            findEntry(database, actor.enumName) == nullptr) {
            actors.push_back(actor.enumName);
        }
    }
    return actors;
}

ActorDatabase
makeSeededDatabase(const hackeroot::Workspace& workspace, const ActorDatabase& builtIn,
                   const std::function<void(usize done, usize total)>& reportProgress) {
    ActorDatabase database{.checkedAgainst = builtIn.checkedAgainst, .entries = builtIn.entries};
    const std::vector<std::string> missing = listNewActors(workspace.getTables(), database);
    const std::vector<hackeroot::ActorEntry>& table = workspace.getTables().actors;
    for (usize i = 0; i < missing.size(); ++i) {
        reportProgress(i, missing.size());
        const auto actor = std::ranges::find(table, missing[i], &hackeroot::ActorEntry::enumName);
        database.entries.emplace(missing[i], deriveEntry(workspace, builtIn, *actor));
    }
    reportProgress(missing.size(), missing.size());
    return database;
}

} // namespace toki::actor
