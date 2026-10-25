#pragma once

#include "app/core/Types.h"

#include <optional>
#include <string>
#include <string_view>

namespace toki::core {

/// An integer input filling its row that is edited as text and taken once it
/// lets go, so typing a number is one edit rather than one per key. It holds
/// what is typed meanwhile, for one field at a time.
class NumberField {
public:
    /// Draws the field `id` showing `value`, and returns what was typed,
    /// clamped to `minimum` and `maximum`, once it lets go of an edit.
    [[nodiscard]] std::optional<s64> draw(std::string_view id, s64 value, s64 minimum, s64 maximum);

private:
    struct HeldValue {
        std::string id;
        s64 value = 0;
    };

    std::optional<HeldValue> held;
};

} // namespace toki::core
