#include "ListEditor.h"

#include "app/scene/Types.h"
#include "app/scene/domain/HeaderSymbols.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"
#include "z64/Relocation.h"
#include "z64/SymbolData.h"

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

[[nodiscard]] base::Diagnostic makeEditError(std::string message) {
    return base::makeError({}, std::move(message));
}

} // namespace

std::expected<ListSite, base::Diagnostic> ListEditor::findIn(const std::string& header) const {
    const z64::SymbolData* commands = model.findSymbol(header);
    if (commands == nullptr) {
        return std::unexpected(makeEditError(std::format("no header '{}' was read", header)));
    }
    const std::optional<usize> command = findCommand(*commands, shape.code);
    const std::optional<std::string> pointee =
        command ? findPointee(*commands, *command + HeaderPointerSize) : std::nullopt;
    if (!pointee || model.findSymbol(*pointee) == nullptr) {
        return std::unexpected(makeEditError(std::format("'{}' has no {}", header, shape.noun)));
    }
    return ListSite{.header = header,
                    .command = *command,
                    .list = *pointee,
                    .count = commands->bytes[*command + 1]};
}

bool ListEditor::hasCommand(const std::string& header) const {
    const z64::SymbolData* commands = model.findSymbol(header);
    return commands != nullptr && findCommand(*commands, shape.code);
}

std::string ListEditor::makeListName() const {
    return makeFreeName(model,
                        std::format("{}_header{:02}_{}", model.getImages()[site.image].front().name,
                                    site.layer, shape.suffix));
}

std::expected<ListSite, base::Diagnostic> ListEditor::find() const {
    const std::expected<std::string, base::Diagnostic> header = findHeaderName(model, site);
    if (!header) {
        return std::unexpected(header.error());
    }
    return findIn(*header);
}

std::expected<ListSite, base::Diagnostic> ListEditor::own() const {
    const std::expected<std::string, base::Diagnostic> header = ownHeader(model, site);
    if (!header) {
        return std::unexpected(header.error());
    }
    std::expected<ListSite, base::Diagnostic> found = findIn(*header);
    if (!found || countPointers(model, found->list) <= 1) {
        return found;
    }
    z64::SymbolData copy = *model.findSymbol(found->list);
    copy.name = makeListName();
    copy.startsFile = false;
    z64::SymbolData commands = *model.findSymbol(found->header);
    std::ranges::find(commands.relocations, found->command + HeaderPointerSize,
                      &z64::Relocation::offset)
        ->symbol = copy.name;
    found->list = copy.name;
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(site.image, std::move(copy));
        !added) {
        return std::unexpected(added.error());
    }
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(commands)); !set) {
        return std::unexpected(set.error());
    }
    return found;
}

std::expected<ListSite, base::Diagnostic> ListEditor::create(const std::string& header,
                                                             const usize count) const {
    z64::SymbolData list{.name = makeListName(),
                         .type = std::string(shape.type),
                         .isArray = true,
                         .alignment = 4,
                         .bytes = std::vector<u8>(count * shape.entrySize)};
    z64::SymbolData commands = *model.findSymbol(header);
    const std::optional<usize> command = insertCommand(commands, shape.code);
    if (!command) {
        return std::unexpected(makeEditError(
            std::format("'{}' has no SCENE_CMD_END to add a {} before", header, shape.noun)));
    }
    commands.bytes[*command + 1] = static_cast<u8>(count);
    commands.relocations.push_back({.offset = *command + HeaderPointerSize, .symbol = list.name});
    std::ranges::sort(commands.relocations, {}, &z64::Relocation::offset);
    ListSite created{.header = header, .command = *command, .list = list.name, .count = count};
    if (std::expected<void, base::Diagnostic> added = model.addSymbol(site.image, std::move(list));
        !added) {
        return std::unexpected(added.error());
    }
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(commands)); !set) {
        return std::unexpected(set.error());
    }
    return created;
}

std::expected<void, base::Diagnostic> ListEditor::setCount(const ListSite& found,
                                                           const usize count) const {
    z64::SymbolData header = *model.findSymbol(found.header);
    header.bytes[found.command + 1] = static_cast<u8>(count);
    return model.setSymbol(std::move(header));
}

std::expected<void, base::Diagnostic> ListEditor::removeList(const ListSite& found) const {
    z64::SymbolData header = *model.findSymbol(found.header);
    eraseBytes(header, found.command, HeaderCommandSize);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(header)); !set) {
        return std::unexpected(set.error());
    }
    if (countPointers(model, found.list) == 0) {
        return model.removeSymbol(found.list);
    }
    return {};
}

std::expected<usize, base::Diagnostic> ListEditor::addEntry() const {
    const std::expected<std::string, base::Diagnostic> header = ownHeader(model, site);
    if (!header) {
        return std::unexpected(header.error());
    }
    if (!hasCommand(*header)) {
        const std::expected<ListSite, base::Diagnostic> created = create(*header, 1);
        if (!created) {
            return std::unexpected(created.error());
        }
        return 0;
    }
    const std::expected<ListSite, base::Diagnostic> found = own();
    if (!found) {
        return std::unexpected(found.error());
    }
    if (found->count >= MaximumEntries) {
        return std::unexpected(makeEditError(std::format(
            "'{}' holds {} entries, as many as its command can count", found->list, found->count)));
    }
    z64::SymbolData entries = *model.findSymbol(found->list);
    insertBytes(entries, found->count * shape.entrySize, shape.entrySize);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(entries)); !set) {
        return std::unexpected(set.error());
    }
    if (std::expected<void, base::Diagnostic> set = setCount(*found, found->count + 1); !set) {
        return std::unexpected(set.error());
    }
    return found->count;
}

std::expected<void, base::Diagnostic> ListEditor::removeEntry(const usize index) const {
    const std::expected<ListSite, base::Diagnostic> read = find();
    if (!read) {
        return std::unexpected(read.error());
    }
    if (index >= read->count) {
        return std::unexpected(makeEditError(std::format(
            "'{}' has {} entries, so there is none at {}", read->list, read->count, index)));
    }
    const std::expected<ListSite, base::Diagnostic> found = own();
    if (!found) {
        return std::unexpected(found.error());
    }
    if (found->count == 1) {
        return removeList(*found);
    }
    z64::SymbolData entries = *model.findSymbol(found->list);
    eraseBytes(entries, index * shape.entrySize, shape.entrySize);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(entries)); !set) {
        return std::unexpected(set.error());
    }
    return setCount(*found, found->count - 1);
}

std::expected<void, base::Diagnostic> ListEditor::setEntries(std::vector<u8> bytes) const {
    const usize count = bytes.size() / shape.entrySize;
    if (count > MaximumEntries) {
        return std::unexpected(makeEditError(std::format("a {} holds at most {} entries, not {}",
                                                         shape.noun, MaximumEntries, count)));
    }
    const std::expected<std::string, base::Diagnostic> header = ownHeader(model, site);
    if (!header) {
        return std::unexpected(header.error());
    }
    if (!hasCommand(*header)) {
        if (count == 0) {
            return {};
        }
        const std::expected<ListSite, base::Diagnostic> created = create(*header, count);
        if (!created) {
            return std::unexpected(created.error());
        }
        z64::SymbolData entries = *model.findSymbol(created->list);
        entries.bytes = std::move(bytes);
        return model.setSymbol(std::move(entries));
    }
    const std::expected<ListSite, base::Diagnostic> found = own();
    if (!found) {
        return std::unexpected(found.error());
    }
    if (count == 0) {
        return removeList(*found);
    }
    z64::SymbolData entries = *model.findSymbol(found->list);
    entries.bytes = std::move(bytes);
    if (std::expected<void, base::Diagnostic> set = model.setSymbol(std::move(entries)); !set) {
        return std::unexpected(set.error());
    }
    return setCount(*found, count);
}

} // namespace toki::scene
