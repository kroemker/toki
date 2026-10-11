#pragma once

#include <string>
#include <string_view>

namespace toki::actor {

/// A problem with an entry as a person reads it: where in the entry, and what.
struct ProblemText {
    std::string place;
    std::string message;
};

/// `message`, as `checkActorEntry` and `checkCollections` write it with the
/// path into `actor`'s entry in front, `ACTOR_EN_BOX.variants[0].parts[1]: draws
/// nothing`, told as "Variant 1 › Part 2" and "Draws nothing". A message with no
/// path is all message.
[[nodiscard]] ProblemText makeProblemText(std::string_view actor, std::string_view message);

} // namespace toki::actor
