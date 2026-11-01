#include "MessageDraft.h"

#include <utility>

namespace toki::message {

bool MessageDraft::setRead(const MessageTable& readTable) {
    read = readTable;
    ++revision;
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
    ++revision;
}

void MessageDraft::markSaved() {
    read = table;
    edited = false;
    readChanged = false;
    ++revision;
}

void MessageDraft::markEdited() {
    edited = true;
    ++revision;
}

void MessageDraft::setText(const usize message, const Language language,
                           std::vector<Piece> pieces) {
    MessageText& text = table.messages[message].texts[static_cast<usize>(language)];
    text.pieces = std::move(pieces);
    text.isPresent = true;
    markEdited();
}

void MessageDraft::setBoxType(const usize message, std::string boxType) {
    table.messages[message].boxType = std::move(boxType);
    markEdited();
}

void MessageDraft::setBoxPosition(const usize message, std::string boxPosition) {
    table.messages[message].boxPosition = std::move(boxPosition);
    markEdited();
}

} // namespace toki::message
