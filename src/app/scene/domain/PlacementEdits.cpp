#include "PlacementEdits.h"

#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize CommandSize = 8;
constexpr usize EntrySize = 0x10;
constexpr usize ObjectIdSize = 2;
constexpr usize PointerSize = 4;
constexpr usize MaximumCommands = 64;
constexpr usize MaximumEntries = 0xFF;

enum class CommandCode : u8 {
    PlayerEntryList = 0x00,
    ActorList = 0x01,
    ObjectList = 0x0B,
    TransitionActorList = 0x0E,
    End = 0x14,
    AlternateHeaderList = 0x18,
};

/// A list a header command points at and counts.
struct ListShape {
    CommandCode code = CommandCode::ActorList;
    usize entrySize = 0;
    std::string_view noun;
    /// The suffix fast64 names a header's list with.
    std::string_view suffix;
    /// Whether a room's header holds it, rather than the scene's.
    bool isRoom = false;
};

constexpr ListShape ObjectListShape{.code = CommandCode::ObjectList,
                                    .entrySize = ObjectIdSize,
                                    .noun = "object list",
                                    .suffix = "objectList",
                                    .isRoom = true};

[[nodiscard]] ListShape getShape(const PlacementKind kind) {
    switch (kind) {
    case PlacementKind::Actor:
        return {.code = CommandCode::ActorList,
                .entrySize = EntrySize,
                .noun = "actor list",
                .suffix = "actorList",
                .isRoom = true};
    case PlacementKind::TransitionActor:
        return {.code = CommandCode::TransitionActorList,
                .entrySize = EntrySize,
                .noun = "transition actor list",
                .suffix = "transitionActors"};
    case PlacementKind::PlayerEntry:
        return {.code = CommandCode::PlayerEntryList,
                .entrySize = EntrySize,
                .noun = "player entry list",
                .suffix = "playerEntryList"};
    }
    return {};
}

[[nodiscard]] base::Diagnostic makeEditError(std::string message) {
    return base::makeError({}, std::move(message));
}

void writeS16(std::vector<u8>& bytes, const usize at, const s16 value) {
    const auto word = static_cast<u16>(value);
    bytes[at] = static_cast<u8>(word >> 8);
    bytes[at + 1] = static_cast<u8>(word);
}

[[nodiscard]] std::optional<std::string> findPointee(const z64::SymbolData& symbol,
                                                     const usize offset) {
    const auto found = std::ranges::find(symbol.relocations, offset, &z64::Relocation::offset);
    if (found == symbol.relocations.end()) {
        return std::nullopt;
    }
    return found->symbol;
}

[[nodiscard]] std::optional<usize> findCommand(const z64::SymbolData& header,
                                               const CommandCode code) {
    for (usize at = 0;
         at + CommandSize <= header.bytes.size() && at < MaximumCommands * CommandSize;
         at += CommandSize) {
        if (header.bytes[at] == static_cast<u8>(code)) {
            return at;
        }
        if (header.bytes[at] == static_cast<u8>(CommandCode::End)) {
            break;
        }
    }
    return std::nullopt;
}

/// Inserts `size` zero bytes at `at`, moving the relocations after it.
void insertBytes(z64::SymbolData& symbol, const usize at, const usize size) {
    symbol.bytes.insert(symbol.bytes.begin() + static_cast<std::ptrdiff_t>(at), size, 0);
    for (z64::Relocation& relocation : symbol.relocations) {
        if (relocation.offset >= at) {
            relocation.offset += size;
        }
    }
}

/// Removes `size` bytes at `at`, and the relocations in them.
void eraseBytes(z64::SymbolData& symbol, const usize at, const usize size) {
    const auto start = symbol.bytes.begin() + static_cast<std::ptrdiff_t>(at);
    symbol.bytes.erase(start, start + static_cast<std::ptrdiff_t>(size));
    std::erase_if(symbol.relocations, [at, size](const z64::Relocation& relocation) {
        return relocation.offset >= at && relocation.offset < at + size;
    });
    for (z64::Relocation& relocation : symbol.relocations) {
        if (relocation.offset >= at + size) {
            relocation.offset -= size;
        }
    }
}

/// A list where one header points at it.
struct ListSite {
    std::string header;
    usize command = 0;
    std::string list;
    usize count = 0;
};

class ListEditor {
public:
    ListEditor(SceneModel& model, const PlacementList& list)
        : ListEditor(model, getShape(list.kind), list.layer, list.room) {}

    ListEditor(SceneModel& model, const ListShape& shape, const usize layer, const usize room)
        : model(model), shape(shape), layer(layer), room(room), image(shape.isRoom ? room + 1 : 0) {
    }

    /// The list the header points at, as it is.
    [[nodiscard]] std::expected<ListSite, base::Diagnostic> find() const;

    /// The list the header points at, copied for it first when another
    /// header points at it too.
    [[nodiscard]] std::expected<ListSite, base::Diagnostic> own() const;

    /// Adds an empty entry to the end of the list, and returns its index.
    [[nodiscard]] std::expected<usize, base::Diagnostic> addEntry() const;

    [[nodiscard]] std::expected<void, base::Diagnostic> removeEntry(usize index) const;

    /// Sets the bytes of the entry at `index` with `write`.
    template <typename Write>
    [[nodiscard]] std::expected<void, base::Diagnostic> setEntry(const usize index,
                                                                 const Write& write) const {
        std::expected<ListSite, base::Diagnostic> site = own();
        if (!site) {
            return std::unexpected(site.error());
        }
        if (index >= site->count) {
            return std::unexpected(makeEditError(std::format(
                "'{}' has {} entries, so there is none at {}", site->list, site->count, index)));
        }
        z64::SymbolData entries = *model.findSymbol(site->list);
        write(entries.bytes, index * shape.entrySize);
        return model.setSymbol(std::move(entries));
    }

private:
    [[nodiscard]] std::expected<std::string, base::Diagnostic> findHeader() const;
    [[nodiscard]] usize countPointers(std::string_view name) const;
    [[nodiscard]] std::string makeCopyName() const;
    [[nodiscard]] std::expected<void, base::Diagnostic> setCount(const ListSite& site,
                                                                 usize count) const;

    SceneModel& model;
    ListShape shape;
    usize layer;
    usize room;
    usize image;
};

std::expected<std::string, base::Diagnostic> ListEditor::findHeader() const {
    if (image >= model.getImages().size() || model.getImages()[image].empty()) {
        return std::unexpected(makeEditError(std::format("the scene has no room {}", room)));
    }
    const z64::SymbolData& main = model.getImages()[image].front();
    if (layer == 0) {
        return main.name;
    }
    const std::optional<usize> command = findCommand(main, CommandCode::AlternateHeaderList);
    const std::optional<std::string> alternates =
        command ? findPointee(main, *command + PointerSize) : std::nullopt;
    const z64::SymbolData* headers = alternates ? model.findSymbol(*alternates) : nullptr;
    const std::optional<std::string> header =
        headers != nullptr ? findPointee(*headers, (layer - 1) * PointerSize) : std::nullopt;
    if (header) {
        return *header;
    }
    if (shape.isRoom) {
        return main.name;
    }
    return std::unexpected(
        makeEditError(std::format("the scene has no header for layer {}", layer)));
}

std::expected<ListSite, base::Diagnostic> ListEditor::find() const {
    std::expected<std::string, base::Diagnostic> header = findHeader();
    if (!header) {
        return std::unexpected(header.error());
    }
    const z64::SymbolData* commands = model.findSymbol(*header);
    if (commands == nullptr) {
        return std::unexpected(makeEditError(std::format("no header '{}' was read", *header)));
    }
    const std::optional<usize> command = findCommand(*commands, shape.code);
    const std::optional<std::string> pointee =
        command ? findPointee(*commands, *command + PointerSize) : std::nullopt;
    if (!pointee || model.findSymbol(*pointee) == nullptr) {
        return std::unexpected(makeEditError(std::format(
            "'{}' has no {}, and adding one is not supported yet", *header, shape.noun)));
    }
    return ListSite{.header = std::move(*header),
                    .command = *command,
                    .list = *pointee,
                    .count = commands->bytes[*command + 1]};
}

usize ListEditor::countPointers(const std::string_view name) const {
    usize count = 0;
    for (const std::vector<z64::SymbolData>& symbols : model.getImages()) {
        for (const z64::SymbolData& symbol : symbols) {
            count += static_cast<usize>(
                std::ranges::count(symbol.relocations, name, &z64::Relocation::symbol));
        }
    }
    return count;
}

std::string ListEditor::makeCopyName() const {
    const std::string base = std::format("{}_header{:02}_{}", model.getImages()[image].front().name,
                                         layer, shape.suffix);
    std::string name = base;
    for (usize suffix = 1; model.findSymbol(name) != nullptr; ++suffix) {
        name = std::format("{}_{}", base, suffix);
    }
    return name;
}

std::expected<ListSite, base::Diagnostic> ListEditor::own() const {
    std::expected<ListSite, base::Diagnostic> site = find();
    if (!site || countPointers(site->list) <= 1) {
        return site;
    }
    z64::SymbolData copy = *model.findSymbol(site->list);
    copy.name = makeCopyName();
    copy.startsFile = false;
    z64::SymbolData header = *model.findSymbol(site->header);
    std::ranges::find(header.relocations, site->command + PointerSize, &z64::Relocation::offset)
        ->symbol = copy.name;
    site->list = copy.name;
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(image, std::move(copy));
        !added) {
        return std::unexpected(added.error());
    }
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(header)); !set) {
        return std::unexpected(set.error());
    }
    return site;
}

std::expected<void, base::Diagnostic> ListEditor::setCount(const ListSite& site,
                                                           const usize count) const {
    z64::SymbolData header = *model.findSymbol(site.header);
    header.bytes[site.command + 1] = static_cast<u8>(count);
    return model.setSymbol(std::move(header));
}

std::expected<usize, base::Diagnostic> ListEditor::addEntry() const {
    std::expected<ListSite, base::Diagnostic> site = own();
    if (!site) {
        return std::unexpected(site.error());
    }
    if (site->count >= MaximumEntries) {
        return std::unexpected(makeEditError(std::format(
            "'{}' holds {} entries, as many as its command can count", site->list, site->count)));
    }
    z64::SymbolData entries = *model.findSymbol(site->list);
    insertBytes(entries, site->count * shape.entrySize, shape.entrySize);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(entries)); !set) {
        return std::unexpected(set.error());
    }
    if (std::expected<void, base::Diagnostic> set = setCount(*site, site->count + 1); !set) {
        return std::unexpected(set.error());
    }
    return site->count;
}

std::expected<void, base::Diagnostic> ListEditor::removeEntry(const usize index) const {
    if (shape.code == CommandCode::PlayerEntryList) {
        return std::unexpected(makeEditError(
            "a player entry is not removed: the spawn list names the entries by index"));
    }
    std::expected<ListSite, base::Diagnostic> site = find();
    if (!site) {
        return std::unexpected(site.error());
    }
    if (index >= site->count) {
        return std::unexpected(makeEditError(std::format(
            "'{}' has {} entries, so there is none at {}", site->list, site->count, index)));
    }
    if (site->count > 1) {
        site = own();
        if (!site) {
            return std::unexpected(site.error());
        }
        z64::SymbolData entries = *model.findSymbol(site->list);
        eraseBytes(entries, index * shape.entrySize, shape.entrySize);
        if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(entries)); !set) {
            return std::unexpected(set.error());
        }
        return setCount(*site, site->count - 1);
    }
    z64::SymbolData header = *model.findSymbol(site->header);
    eraseBytes(header, site->command, CommandSize);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(header)); !set) {
        return std::unexpected(set.error());
    }
    if (countPointers(site->list) == 0) {
        return model.removeSymbol(site->list);
    }
    return {};
}

[[nodiscard]] std::expected<void, base::Diagnostic> requireKind(const PlacementList& list,
                                                                const bool isTransition) {
    if ((list.kind == PlacementKind::TransitionActor) != isTransition) {
        return std::unexpected(makeEditError(
            std::format("a {} does not hold this kind of placement", getShape(list.kind).noun)));
    }
    return {};
}

void writeActor(std::vector<u8>& bytes, const usize at, const ActorPlacement& placement) {
    writeS16(bytes, at, placement.id);
    for (usize axis = 0; axis < 3; ++axis) {
        writeS16(bytes, at + 2 + axis * 2, placement.position[axis]);
        writeS16(bytes, at + 8 + axis * 2, placement.rotation[axis]);
    }
    writeS16(bytes, at + 14, placement.params);
}

void writeTransitionActor(std::vector<u8>& bytes, const usize at,
                          const TransitionActorPlacement& placement) {
    bytes[at] = static_cast<u8>(placement.rooms[0]);
    bytes[at + 2] = static_cast<u8>(placement.rooms[1]);
    writeS16(bytes, at + 4, placement.id);
    for (usize axis = 0; axis < 3; ++axis) {
        writeS16(bytes, at + 6 + axis * 2, placement.position[axis]);
    }
    writeS16(bytes, at + 12, placement.rotationY);
    writeS16(bytes, at + 14, placement.params);
}

} // namespace

std::expected<void, base::Diagnostic> setPlacement(SceneModel& model, const PlacementList& list,
                                                   const usize index,
                                                   const ActorPlacement& placement) {
    if (std::expected<void, base::Diagnostic> kind = requireKind(list, false); !kind) {
        return kind;
    }
    return ListEditor(model, list)
        .setEntry(index, [&placement](std::vector<u8>& bytes, const usize at) {
            writeActor(bytes, at, placement);
        });
}

std::expected<void, base::Diagnostic>
setTransitionActor(SceneModel& model, const usize layer, const usize index,
                   const TransitionActorPlacement& placement) {
    const PlacementList list{.kind = PlacementKind::TransitionActor, .layer = layer};
    return ListEditor(model, list)
        .setEntry(index, [&placement](std::vector<u8>& bytes, const usize at) {
            writeTransitionActor(bytes, at, placement);
        });
}

std::expected<usize, base::Diagnostic> addPlacement(SceneModel& model, const PlacementList& list,
                                                    const ActorPlacement& placement) {
    if (std::expected<void, base::Diagnostic> kind = requireKind(list, false); !kind) {
        return std::unexpected(kind.error());
    }
    const ListEditor editor(model, list);
    const std::expected<usize, base::Diagnostic> index = editor.addEntry();
    if (!index) {
        return index;
    }
    if (std::expected<void, base::Diagnostic> set = setPlacement(model, list, *index, placement);
        !set) {
        return std::unexpected(set.error());
    }
    return index;
}

std::expected<usize, base::Diagnostic>
addTransitionActor(SceneModel& model, const usize layer,
                   const TransitionActorPlacement& placement) {
    const ListEditor editor(model, {.kind = PlacementKind::TransitionActor, .layer = layer});
    const std::expected<usize, base::Diagnostic> index = editor.addEntry();
    if (!index) {
        return index;
    }
    if (std::expected<void, base::Diagnostic> set =
            setTransitionActor(model, layer, *index, placement);
        !set) {
        return std::unexpected(set.error());
    }
    return index;
}

std::expected<void, base::Diagnostic> addRoomObjects(SceneModel& model, const usize layer,
                                                     const usize room,
                                                     const std::span<const s16> objects) {
    const ListEditor editor(model, ObjectListShape, layer, room);
    for (const s16 object : objects) {
        const std::expected<usize, base::Diagnostic> index = editor.addEntry();
        if (!index) {
            return std::unexpected(index.error());
        }
        if (std::expected<void, base::Diagnostic> set =
                editor.setEntry(*index, [object](std::vector<u8>& bytes,
                                                 const usize at) { writeS16(bytes, at, object); });
            !set) {
            return set;
        }
    }
    return {};
}

std::expected<void, base::Diagnostic> removePlacement(SceneModel& model, const PlacementList& list,
                                                      const usize index) {
    return ListEditor(model, list).removeEntry(index);
}

} // namespace toki::scene
