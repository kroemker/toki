#pragma once

#include "app/core/Types.h"

#include <functional>
#include <string>
#include <vector>

namespace toki::core {

/// What the person answered when asked about edits no save wrote.
enum class UnsavedChoice : u8 {
    Save,
    DontSave,
    Cancel,
};

/// Asks, before something would throw edits away, whether to save them: one
/// question for every kind of asset, naming each by its tab's title.
class UnsavedEditsDialog {
public:
    using Answered = std::function<void(UnsavedChoice)>;

    /// Asks about the edits of the assets `titles` before `doing`, a verb in
    /// its `-ing` form: `closing`, `quitting`. `answered` is called once the
    /// person picks.
    void ask(std::vector<std::string> titles, std::string doing, Answered answered);

    [[nodiscard]] bool isAsking() const {
        return answered != nullptr;
    }

    /// Draws the question while it is asked; called every frame.
    void draw();

private:
    void answer(UnsavedChoice choice);

    std::vector<std::string> titles;
    std::string doing;
    Answered answered;
};

} // namespace toki::core
