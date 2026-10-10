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

std::expected<void, std::string> setCheckoutEntry(ActorDatabase& checkout, ActorEntry entry) {
    if (entry.actor.empty()) {
        return std::unexpected(std::string("an entry needs the enum name of its actor"));
    }
    std::string actor = entry.actor;
    checkout.entries.insert_or_assign(std::move(actor), std::move(entry));
    return {};
}

std::expected<void, std::string> removeCheckoutEntry(ActorDatabase& checkout,
                                                     const std::string& actor) {
    if (checkout.entries.erase(actor) == 0) {
        return std::unexpected(
            std::format("the checkout's actor database has no entry for {} to remove", actor));
    }
    return {};
}

} // namespace toki::actor
