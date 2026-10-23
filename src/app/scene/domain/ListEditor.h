#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/HeaderSymbols.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "z64/SymbolData.h"

#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

/// A list a header command points at and counts in the byte after its ID.
struct ListShape {
    CommandCode code = CommandCode::ActorList;
    usize entrySize = 0;
    /// The type a list made for a header is declared with.
    std::string_view type;
    std::string_view noun;
    /// The suffix fast64 names a header's list with.
    std::string_view suffix;
    /// Whether a room's header holds it, rather than the scene's.
    bool isRoom = false;
};

/// A list where one header points at it.
struct ListSite {
    std::string header;
    usize command = 0;
    std::string list;
    usize count = 0;
};

/// Edits the list of one shape that the header of a layer points at, in the
/// scene or in a room. A header or a list another layer reads too is copied
/// for this layer first, so an edit of one layer never shows in another.
class ListEditor {
public:
    /// The most entries a command's count byte holds.
    static constexpr usize MaximumEntries = 0xFF;

    ListEditor(SceneModel& model, const ListShape& shape, const usize layer, const usize room)
        : model(model), shape(shape), site{.image = shape.isRoom ? room + 1 : 0, .layer = layer} {}

    /// The list the header points at, as it is.
    [[nodiscard]] std::expected<ListSite, base::Diagnostic> find() const;

    /// The list the header points at, the header and the list each copied
    /// for this layer first when another layer reads it too.
    [[nodiscard]] std::expected<ListSite, base::Diagnostic> own() const;

    /// Adds an empty entry to the end of the list, made for the header when
    /// it has none, and returns its index.
    [[nodiscard]] std::expected<usize, base::Diagnostic> addEntry() const;

    /// Removes the entry at `index`. Removing the last one removes the list's
    /// command from the header, and the list once nothing points at it.
    [[nodiscard]] std::expected<void, base::Diagnostic> removeEntry(usize index) const;

    /// Replaces every entry with `bytes`, an entry after another. No entries
    /// remove the list from the header, as removing the last one does.
    [[nodiscard]] std::expected<void, base::Diagnostic> setEntries(std::vector<u8> bytes) const;

    /// Sets the bytes of the entry at `index` with `write`.
    template <typename Write>
    [[nodiscard]] std::expected<void, base::Diagnostic> setEntry(const usize index,
                                                                 const Write& write) const {
        std::expected<ListSite, base::Diagnostic> found = own();
        if (!found) {
            return std::unexpected(found.error());
        }
        if (index >= found->count) {
            return std::unexpected(
                base::makeError({}, std::format("'{}' has {} entries, so there is none at {}",
                                                found->list, found->count, index)));
        }
        z64::SymbolData entries = *model.findSymbol(found->list);
        write(entries.bytes, index * shape.entrySize);
        return model.setSymbol(std::move(entries));
    }

private:
    [[nodiscard]] std::expected<ListSite, base::Diagnostic> findIn(const std::string& header) const;
    [[nodiscard]] bool hasCommand(const std::string& header) const;
    [[nodiscard]] std::string makeListName() const;
    [[nodiscard]] std::expected<ListSite, base::Diagnostic> create(const std::string& header,
                                                                   usize count) const;
    [[nodiscard]] std::expected<void, base::Diagnostic> setCount(const ListSite& found,
                                                                 usize count) const;
    [[nodiscard]] std::expected<void, base::Diagnostic> removeList(const ListSite& found) const;

    SceneModel& model;
    ListShape shape;
    HeaderSite site;
};

} // namespace toki::scene
