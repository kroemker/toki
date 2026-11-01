#pragma once

#include "app/core/Types.h"

#include <optional>

namespace toki::core {

/// A panel whose selection the Properties panel can show.
class PropertySource {
public:
    PropertySource() = default;
    PropertySource(const PropertySource&) = delete;
    PropertySource& operator=(const PropertySource&) = delete;
    PropertySource(PropertySource&&) = delete;
    PropertySource& operator=(PropertySource&&) = delete;
    virtual ~PropertySource() = default;

    /// What is selected, as a number that changes whenever the selection
    /// does; nothing when nothing is selected.
    [[nodiscard]] virtual std::optional<u64> findSelectionKey() const = 0;

    /// Draws the form of what is selected, or says what to select.
    virtual void drawProperties() = 0;
};

} // namespace toki::core
