#pragma once

#include "app/message/Types.h"
#include "app/message/domain/Message.h"

#include <string>
#include <vector>

namespace toki::message {

/// The messages as they are being edited: the table last read, and every edit
/// made since. A table read again replaces the draft only while nothing is
/// edited; otherwise the draft is kept and says the checkout changed under it,
/// so no edit is lost without asking.
class MessageDraft {
public:
    /// Takes `table` as read. Returns whether the draft is it now.
    bool setRead(const MessageTable& table);

    /// Throws every edit away and goes back to the table last read.
    void discard();

    /// Takes the draft as what was read, once it is written to the checkout,
    /// so that the table read back from it replaces it.
    void markSaved();

    [[nodiscard]] const MessageTable& getTable() const {
        return table;
    }

    [[nodiscard]] bool isEdited() const {
        return edited;
    }

    /// Whether the checkout's messages were read again while edits were kept.
    [[nodiscard]] bool hasReadChanged() const {
        return readChanged;
    }

    /// A number that changes with every edit and every table taken.
    [[nodiscard]] u64 getRevision() const {
        return revision;
    }

    void setText(usize message, Language language, std::vector<Piece> pieces);
    void setBoxType(usize message, std::string boxType);
    void setBoxPosition(usize message, std::string boxPosition);

private:
    void markEdited();

    MessageTable table;
    MessageTable read;
    bool edited = false;
    bool readChanged = false;
    u64 revision = 0;
};

} // namespace toki::message
