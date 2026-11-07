#include "MessageDraft.h"

#include "app/message/domain/MessageMarkup.h"

#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace toki::message {

namespace {

[[nodiscard]] bool isSameText(const MessageText& left, const MessageText& right) {
    return left.isPresent == right.isPresent && left.placeholder == right.placeholder &&
           formatMarkup(left.pieces) == formatMarkup(right.pieces);
}

[[nodiscard]] bool isSameMessage(const Message& left, const Message& right) {
    if (left.boxType != right.boxType || left.boxPosition != right.boxPosition) {
        return false;
    }
    for (usize language = 0; language < LanguageCount; ++language) {
        if (!isSameText(left.texts[language], right.texts[language])) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<MessageKey> MessageDraft::setRead(const MessageTable& readTable) {
    MessageTable newTable = readTable;
    std::set<usize> newEdited;
    std::set<usize> newReadChanged;
    std::vector<MessageKey> dropped;
    for (const usize message : edited) {
        const MessageKey key = makeMessageKey(table, message);
        const std::optional<usize> found = findMessage(readTable, key);
        if (!found) {
            dropped.push_back(key);
            continue;
        }
        newTable.messages[*found] = std::move(table.messages[message]);
        newEdited.insert(*found);
        if (readChanged.contains(message) ||
            !isSameMessage(read.messages[message], readTable.messages[*found])) {
            newReadChanged.insert(*found);
        }
    }
    table = std::move(newTable);
    read = readTable;
    edited = std::move(newEdited);
    readChanged = std::move(newReadChanged);
    markReplaced();
    return dropped;
}

void MessageDraft::discard() {
    table = read;
    edited.clear();
    readChanged.clear();
    markReplaced();
}

void MessageDraft::discard(const usize message) {
    if (!edited.contains(message)) {
        return;
    }
    table.messages[message] = read.messages[message];
    markEdited(message);
}

MessageTable MessageDraft::makeSavedTable(const std::span<const usize> messages) const {
    MessageTable saved = read;
    for (const usize message : messages) {
        saved.messages[message] = table.messages[message];
    }
    return saved;
}

void MessageDraft::markSaved(const MessageTable& saved, const std::span<const usize> messages) {
    for (const usize message : messages) {
        if (message >= read.messages.size() || message >= saved.messages.size()) {
            continue;
        }
        read.messages[message] = saved.messages[message];
        if (isSameMessage(table.messages[message], read.messages[message])) {
            edited.erase(message);
            readChanged.erase(message);
        }
    }
    markReplaced();
}

std::optional<usize> MessageDraft::findOnlyEdited(const u64 since) const {
    if (!editedAlone || since < editedAloneSince) {
        return std::nullopt;
    }
    return editedAlone;
}

void MessageDraft::markReplaced() {
    editedAlone.reset();
    ++revision;
}

void MessageDraft::markEdited(const usize message) {
    if (isSameMessage(table.messages[message], read.messages[message])) {
        edited.erase(message);
        readChanged.erase(message);
    } else {
        edited.insert(message);
    }
    if (editedAlone != message) {
        editedAlone = message;
        editedAloneSince = revision;
    }
    ++revision;
}

void MessageDraft::setText(const usize message, const Language language,
                           std::vector<Piece> pieces) {
    MessageText& text = table.messages[message].texts[static_cast<usize>(language)];
    text.pieces = std::move(pieces);
    text.isPresent = true;
    markEdited(message);
}

void MessageDraft::setBoxType(const usize message, std::string boxType) {
    table.messages[message].boxType = std::move(boxType);
    markEdited(message);
}

void MessageDraft::setBoxPosition(const usize message, std::string boxPosition) {
    table.messages[message].boxPosition = std::move(boxPosition);
    markEdited(message);
}

} // namespace toki::message
