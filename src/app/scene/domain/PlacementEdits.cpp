#include "PlacementEdits.h"

#include "app/scene/Types.h"
#include "app/scene/domain/ListEditor.h"
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

constexpr usize EntrySize = 0x10;
constexpr usize ObjectIdSize = 2;

constexpr ListShape ObjectListShape{.code = CommandCode::ObjectList,
                                    .entrySize = ObjectIdSize,
                                    .type = "s16",
                                    .noun = "object list",
                                    .suffix = "objectList",
                                    .isRoom = true};

[[nodiscard]] ListShape getShape(const PlacementKind kind) {
    switch (kind) {
    case PlacementKind::Actor:
        return {.code = CommandCode::ActorList,
                .entrySize = EntrySize,
                .type = "ActorEntry",
                .noun = "actor list",
                .suffix = "actorList",
                .isRoom = true};
    case PlacementKind::TransitionActor:
        return {.code = CommandCode::TransitionActorList,
                .entrySize = EntrySize,
                .type = "TransitionActorEntry",
                .noun = "transition actor list",
                .suffix = "transitionActors"};
    case PlacementKind::PlayerEntry:
        return {.code = CommandCode::PlayerEntryList,
                .entrySize = EntrySize,
                .type = "ActorEntry",
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

[[nodiscard]] ListEditor makeListEditor(SceneModel& model, const PlacementList& list) {
    return {model, getShape(list.kind), list.layer, list.room};
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
    return makeListEditor(model, list)
        .setEntry(index, [&placement](std::vector<u8>& bytes, const usize at) {
            writeActor(bytes, at, placement);
        });
}

std::expected<void, base::Diagnostic>
setTransitionActor(SceneModel& model, const usize layer, const usize index,
                   const TransitionActorPlacement& placement) {
    const PlacementList list{.kind = PlacementKind::TransitionActor, .layer = layer};
    return makeListEditor(model, list)
        .setEntry(index, [&placement](std::vector<u8>& bytes, const usize at) {
            writeTransitionActor(bytes, at, placement);
        });
}

std::expected<usize, base::Diagnostic> addPlacement(SceneModel& model, const PlacementList& list,
                                                    const ActorPlacement& placement) {
    if (std::expected<void, base::Diagnostic> kind = requireKind(list, false); !kind) {
        return std::unexpected(kind.error());
    }
    const ListEditor editor = makeListEditor(model, list);
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
    const ListEditor editor =
        makeListEditor(model, {.kind = PlacementKind::TransitionActor, .layer = layer});
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

std::expected<void, base::Diagnostic> setRoomObjects(SceneModel& model, const usize layer,
                                                     const usize room,
                                                     const std::span<const s16> objects) {
    std::vector<u8> bytes(objects.size() * ObjectIdSize);
    for (usize i = 0; i < objects.size(); ++i) {
        writeS16(bytes, i * ObjectIdSize, objects[i]);
    }
    return ListEditor(model, ObjectListShape, layer, room).setEntries(std::move(bytes));
}

std::expected<void, base::Diagnostic> removePlacement(SceneModel& model, const PlacementList& list,
                                                      const usize index) {
    if (list.kind == PlacementKind::PlayerEntry) {
        return std::unexpected(makeEditError(
            "a player entry is not removed: the spawn list names the entries by index"));
    }
    return makeListEditor(model, list).removeEntry(index);
}

} // namespace toki::scene
