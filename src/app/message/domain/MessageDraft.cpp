#include "MessageDraft.h"

#include "app/message/domain/MessageMarkup.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace toki::message {

namespace {

constexpr usize MaxSteps = 200;

[[nodiscard]] bool isSamePiece(const Piece& left, const Piece& right) {
    if (left.index() != right.index()) {
        return false;
    }
    if (const auto* text = std::get_if<TextPiece>(&left)) {
        return text->text == std::get<TextPiece>(right).text;
    }
    if (const auto* code = std::get_if<CodePiece>(&left)) {
        const auto& other = std::get<CodePiece>(right);
        return code->name == other.name && code->arguments == other.arguments;
    }
    return true;
}

[[nodiscard]] bool isSamePieces(const std::span<const Piece> left,
                                const std::span<const Piece> right) {
    return std::ranges::equal(left, right, isSamePiece);
}

[[nodiscard]] bool isSameText(const MessageText& left, const MessageText& right) {
    return left.isPresent == right.isPresent && left.placeholder == right.placeholder &&
           (isSamePieces(left.pieces, right.pieces) ||
            formatMarkup(left.pieces) == formatMarkup(right.pieces));
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
    std::map<usize, History> newHistories;
    for (auto& [message, history] : histories) {
        const std::optional<usize> found = findMessage(readTable, makeMessageKey(table, message));
        if (found) {
            newHistories[*found] = std::move(history);
        }
    }
    table = std::move(newTable);
    read = readTable;
    edited = std::move(newEdited);
    readChanged = std::move(newReadChanged);
    histories = std::move(newHistories);
    lastStep.reset();
    markReplaced();
    return dropped;
}

void MessageDraft::discard() {
    table = read;
    edited.clear();
    readChanged.clear();
    histories.clear();
    lastStep.reset();
    markReplaced();
}

void MessageDraft::discard(const usize message) {
    if (!edited.contains(message)) {
        return;
    }
    table.messages[message] = read.messages[message];
    histories.erase(message);
    lastStep.reset();
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

u64 MessageDraft::getRevision(const usize message) const {
    const auto found = messageRevisions.find(message);
    return found != messageRevisions.end() ? found->second : tableRevision;
}

void MessageDraft::markReplaced() {
    editedAlone.reset();
    ++revision;
    tableRevision = revision;
    messageRevisions.clear();
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
    messageRevisions[message] = revision;
}

void MessageDraft::beginStep(const usize message, const bool joinsLast) {
    History& history = histories[message];
    history.redone.clear();
    if (!joinsLast || lastStep != message || history.undone.empty()) {
        history.undone.push_back(table.messages[message]);
        if (history.undone.size() > MaxSteps) {
            history.undone.erase(history.undone.begin());
        }
    }
    lastStep = message;
}

bool MessageDraft::canUndo(const usize message) const {
    const auto found = histories.find(message);
    return found != histories.end() && !found->second.undone.empty();
}

bool MessageDraft::canRedo(const usize message) const {
    const auto found = histories.find(message);
    return found != histories.end() && !found->second.redone.empty();
}

bool MessageDraft::undo(const usize message) {
    if (!canUndo(message)) {
        return false;
    }
    History& history = histories[message];
    history.redone.push_back(std::move(table.messages[message]));
    table.messages[message] = std::move(history.undone.back());
    history.undone.pop_back();
    lastStep.reset();
    markEdited(message);
    return true;
}

bool MessageDraft::redo(const usize message) {
    if (!canRedo(message)) {
        return false;
    }
    History& history = histories[message];
    history.undone.push_back(std::move(table.messages[message]));
    table.messages[message] = std::move(history.redone.back());
    history.redone.pop_back();
    lastStep.reset();
    markEdited(message);
    return true;
}

void MessageDraft::setText(const usize message, const Language language, std::vector<Piece> pieces,
                           const bool joinsLast) {
    MessageText& text = table.messages[message].texts[static_cast<usize>(language)];
    if (text.isPresent && isSamePieces(text.pieces, pieces)) {
        return;
    }
    beginStep(message, joinsLast);
    text.pieces = std::move(pieces);
    text.isPresent = true;
    markEdited(message);
}

void MessageDraft::setBoxType(const usize message, std::string boxType) {
    if (table.messages[message].boxType == boxType) {
        return;
    }
    beginStep(message, false);
    table.messages[message].boxType = std::move(boxType);
    markEdited(message);
}

void MessageDraft::setBoxPosition(const usize message, std::string boxPosition) {
    if (table.messages[message].boxPosition == boxPosition) {
        return;
    }
    beginStep(message, false);
    table.messages[message].boxPosition = std::move(boxPosition);
    markEdited(message);
}

} // namespace toki::message
