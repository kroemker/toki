#include "MessageDraft.h"

#include <optional>
#include <utility>

namespace toki::message {

bool MessageDraft::setRead(const MessageTable& readTable) {
    read = readTable;
    markReplaced();
    if (edited) {
        readChanged = true;
        return false;
    }
    table = read;
    return true;
}

void MessageDraft::discard() {
    table = read;
    edited = false;
    readChanged = false;
    markReplaced();
}

void MessageDraft::markSaved() {
    read = table;
    edited = false;
    readChanged = false;
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
    edited = true;
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
