#pragma once

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// What the person answered when asked about new actors.
struct NewActorsAnswer {
    bool isDerived = false;
    std::vector<std::string> actors;
};

/// Asks whether to derive the actors the checkout's actor table lists that its
/// actor database has no entry for: "N new actors were found. Derive them?"
class NewActorsPrompt {
public:
    /// Asks about `actors` once the prompt is next drawn, unless they are the
    /// ones the person already said no to, or there are none.
    void offer(std::vector<std::string> actors, const std::vector<std::string>& dismissed);

    /// The answer, the frame it is given.
    [[nodiscard]] std::optional<NewActorsAnswer> draw();

private:
    std::vector<std::string> offered;
    bool isOpening = false;
};

} // namespace toki::actor
