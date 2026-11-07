#pragma once

#include "app/core/Types.h"

#include <functional>
#include <optional>
#include <string>

namespace toki::core {

/// One asset open for editing in a tab of an `AssetTabPanel`: what it is
/// called, whether it has edits no save wrote, and how it is saved, thrown
/// back to what was read, and drawn. Every editor of an asset kind is one.
class AssetTab {
public:
    /// Called once a save ends, with whether it wrote the asset.
    using Saved = std::function<void(bool isWritten)>;

    AssetTab() = default;
    AssetTab(const AssetTab&) = delete;
    AssetTab& operator=(const AssetTab&) = delete;
    AssetTab(AssetTab&&) = delete;
    AssetTab& operator=(AssetTab&&) = delete;
    virtual ~AssetTab() = default;

    /// What the tab says, `Message 0x1234`; also what asks about its edits
    /// names it by.
    [[nodiscard]] virtual std::string getTitle() const = 0;

    [[nodiscard]] virtual bool isEdited() const = 0;

    /// Writes the asset's edits to the checkout, and calls `saved` once that
    /// is done or has failed, which may be frames later.
    virtual void save(Saved saved) = 0;

    /// Throws the asset's edits away.
    virtual void discard() = 0;

    virtual void draw() = 0;

    /// What the Properties panel shows of the asset, as a number that changes
    /// whenever it does; nothing for a tab that shows nothing there.
    [[nodiscard]] virtual std::optional<u64> findSelectionKey() const {
        return std::nullopt;
    }

    virtual void drawProperties() {}
};

} // namespace toki::core
