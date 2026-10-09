#include "DatabaseEdits.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/FieldReading.h"

#include <expected>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

std::expected<void, std::string> keepGuess(ActorDatabase& checkout, const ActorEntry& guess) {
    if (checkout.entries.contains(guess.actor)) {
        return std::unexpected(std::format(
            "the checkout's actor database already has an entry for {}, which a guess never "
            "replaces",
            guess.actor));
    }
    ActorEntry kept = guess;
    kept.status = EntryStatus::Guessed;
    checkout.entries.emplace(kept.actor, std::move(kept));
    return {};
}

std::expected<void, std::string> addVariantForParams(ActorDatabase& checkout,
                                                     const ActorEntry& current,
                                                     const PlacementValues& placement) {
    if (current.fields.empty()) {
        return std::unexpected(std::format(
            "{} has no fields, so a variant cannot tell these params from others", current.actor));
    }
    ActorEntry& entry = checkout.entries.try_emplace(current.actor, current).first->second;
    const ActorVariant* matched = findVariant(entry, placement);
    ActorVariant variant = matched != nullptr ? *matched : ActorVariant{};
    std::vector<FieldMatch> match = makeFieldsMatch(entry, placement);
    for (const FieldMatch& kept : variant.match) {
        if (isBuiltInField(kept.field)) {
            match.push_back(kept);
        }
    }
    variant.match = std::move(match);
    variant.defaultParams = placement.params;
    variant.location = {};
    entry.variants.insert(entry.variants.begin(), std::move(variant));
    return {};
}

} // namespace toki::actor
