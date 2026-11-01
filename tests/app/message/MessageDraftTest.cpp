#include "app/message/domain/MessageDraft.h"

#include "app/message/domain/Message.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

using toki::message::Language;
using toki::message::Message;
using toki::message::MessageDraft;
using toki::message::MessageTable;
using toki::message::TextPiece;

namespace {

[[nodiscard]] MessageTable makeTable(const std::string& text) {
    MessageTable table;
    Message message{.id = 1, .boxType = "TEXTBOX_TYPE_BLACK"};
    message.texts[static_cast<base::usize>(Language::English)].pieces = {TextPiece{.text = text}};
    table.messages.push_back(message);
    return table;
}

[[nodiscard]] std::string readEnglish(const MessageDraft& draft) {
    const auto& pieces =
        draft.getTable().messages[0].texts[static_cast<base::usize>(Language::English)].pieces;
    return pieces.empty() ? std::string() : std::get<TextPiece>(pieces[0]).text;
}

} // namespace

TEST_CASE("a draft takes each table read while nothing is edited", "[message]") {
    MessageDraft draft;
    CHECK(draft.setRead(makeTable("first")));
    CHECK(draft.setRead(makeTable("second")));
    CHECK(readEnglish(draft) == "second");
    CHECK_FALSE(draft.isEdited());
}

TEST_CASE("a draft keeps its edits over a table read again until they are discarded", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTable("read")));
    const base::u64 before = draft.getRevision();
    draft.setText(0, Language::English, {TextPiece{.text = "edited"}});
    CHECK(draft.isEdited());
    CHECK(draft.getRevision() != before);

    CHECK_FALSE(draft.setRead(makeTable("changed")));
    CHECK(readEnglish(draft) == "edited");
    CHECK(draft.hasReadChanged());

    draft.discard();
    CHECK(readEnglish(draft) == "changed");
    CHECK_FALSE(draft.isEdited());
    CHECK_FALSE(draft.hasReadChanged());
}

TEST_CASE("a saved draft takes the table read back from what it wrote", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTable("read")));
    draft.setText(0, Language::English, {TextPiece{.text = "edited"}});

    draft.markSaved();

    CHECK_FALSE(draft.isEdited());
    CHECK(draft.setRead(makeTable("edited")));
    CHECK(readEnglish(draft) == "edited");
}

TEST_CASE("a draft says which one message every change since a revision edited", "[message]") {
    MessageDraft draft;
    MessageTable table = makeTable("first");
    table.messages.push_back(table.messages[0]);
    static_cast<void>(draft.setRead(table));
    const base::u64 read = draft.getRevision();
    CHECK_FALSE(draft.findOnlyEdited(read).has_value());

    draft.setText(1, Language::English, {TextPiece{.text = "a"}});
    const base::u64 typed = draft.getRevision();
    draft.setText(1, Language::English, {TextPiece{.text = "ab"}});
    CHECK(draft.findOnlyEdited(read) == base::usize{1});
    CHECK(draft.findOnlyEdited(typed) == base::usize{1});

    draft.setBoxType(0, "TEXTBOX_TYPE_BLUE");
    CHECK_FALSE(draft.findOnlyEdited(typed).has_value());
    CHECK(draft.findOnlyEdited(draft.getRevision() - 1) == base::usize{0});

    draft.discard();
    CHECK_FALSE(draft.findOnlyEdited(read).has_value());
}
