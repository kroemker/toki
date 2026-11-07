#include "app/message/domain/MessageDraft.h"

#include "app/message/domain/Message.h"
#include "app/message/domain/MessageKey.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

using toki::message::Language;
using toki::message::Message;
using toki::message::MessageDraft;
using toki::message::MessageTable;
using toki::message::MessageText;
using toki::message::TextPiece;

namespace {

[[nodiscard]] MessageTable makeTableOf(const std::vector<std::string>& texts) {
    MessageTable table;
    for (const std::string& text : texts) {
        Message message{.id = static_cast<base::u16>(table.messages.size() + 1),
                        .boxType = "TEXTBOX_TYPE_BLACK"};
        MessageText& english = message.texts[static_cast<base::usize>(Language::English)];
        english.pieces = {TextPiece{.text = text}};
        english.isPresent = true;
        table.messages.push_back(message);
    }
    return table;
}

[[nodiscard]] MessageTable makeTable(const std::string& text) {
    return makeTableOf({text});
}

[[nodiscard]] std::string readText(const MessageTable& table, const base::usize message) {
    const auto& pieces =
        table.messages[message].texts[static_cast<base::usize>(Language::English)].pieces;
    return pieces.empty() ? std::string() : std::get<TextPiece>(pieces[0]).text;
}

[[nodiscard]] std::string readEnglish(const MessageDraft& draft) {
    return readText(draft.getTable(), 0);
}

} // namespace

TEST_CASE("a draft takes each table read while nothing is edited", "[message]") {
    MessageDraft draft;
    CHECK(draft.setRead(makeTable("first")).empty());
    CHECK(draft.setRead(makeTable("second")).empty());
    CHECK(readEnglish(draft) == "second");
    CHECK_FALSE(draft.isEdited());
}

TEST_CASE("a draft keeps its edits over a table read again until they are discarded", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTable("read")));
    const base::u64 before = draft.getRevision();
    draft.setText(0, Language::English, {TextPiece{.text = "edited"}});
    CHECK(draft.isEdited());
    CHECK(draft.isEdited(0));
    CHECK(draft.getRevision() != before);

    CHECK(draft.setRead(makeTable("changed")).empty());
    CHECK(readEnglish(draft) == "edited");
    CHECK(draft.hasReadChanged(0));

    draft.discard();
    CHECK(readEnglish(draft) == "changed");
    CHECK_FALSE(draft.isEdited());
    CHECK_FALSE(draft.hasReadChanged(0));
}

TEST_CASE("a message edited back to what was read has no edits", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTable("read")));
    draft.setText(0, Language::English, {TextPiece{.text = "edited"}});
    draft.setText(0, Language::English, {TextPiece{.text = "read"}});
    CHECK_FALSE(draft.isEdited());
}

TEST_CASE("a saved message takes the table read back from what was written", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTable("read")));
    draft.setText(0, Language::English, {TextPiece{.text = "edited"}});

    const std::vector<base::usize> saved = {0};
    const MessageTable written = draft.makeSavedTable(saved);
    CHECK(readText(written, 0) == "edited");
    draft.markSaved(written, saved);

    CHECK_FALSE(draft.isEdited());
    CHECK(draft.setRead(makeTable("edited")).empty());
    CHECK(readEnglish(draft) == "edited");
    CHECK_FALSE(draft.isEdited());
}

TEST_CASE("one message is saved or discarded without the others", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTableOf({"a", "b"})));
    draft.setText(0, Language::English, {TextPiece{.text = "a2"}});
    draft.setText(1, Language::English, {TextPiece{.text = "b2"}});

    const std::vector<base::usize> first = {0};
    const MessageTable saved = draft.makeSavedTable(first);
    CHECK(readText(saved, 0) == "a2");
    CHECK(readText(saved, 1) == "b");
    draft.markSaved(saved, first);
    CHECK_FALSE(draft.isEdited(0));
    CHECK(draft.isEdited(1));

    CHECK(draft.setRead(saved).empty());
    CHECK(readText(draft.getTable(), 0) == "a2");
    CHECK(readText(draft.getTable(), 1) == "b2");
    CHECK(draft.isEdited(1));
    CHECK_FALSE(draft.hasReadChanged(1));

    draft.discard(1);
    CHECK(readText(draft.getTable(), 1) == "b");
    CHECK_FALSE(draft.isEdited());
}

TEST_CASE("what a message was edited to while it was saved still counts as edits", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTable("read")));
    draft.setText(0, Language::English, {TextPiece{.text = "a"}});
    const std::vector<base::usize> saved = {0};
    const MessageTable written = draft.makeSavedTable(saved);
    draft.setText(0, Language::English, {TextPiece{.text = "ab"}});
    draft.markSaved(written, saved);
    CHECK(draft.isEdited(0));
    CHECK(readEnglish(draft) == "ab");
}

TEST_CASE("edits follow their message when rows are added before it", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTableOf({"a", "b"})));
    draft.setText(1, Language::English, {TextPiece{.text = "b2"}});

    MessageTable longer = makeTableOf({"a", "b"});
    longer.messages.insert(longer.messages.begin(), makeTable("new").messages[0]);
    longer.messages[0].id = 9;
    CHECK(draft.setRead(longer).empty());
    CHECK(draft.listEdited() == std::vector<base::usize>{2});
    CHECK(readText(draft.getTable(), 2) == "b2");
    CHECK(readText(draft.getTable(), 1) == "a");
}

TEST_CASE("edits of a message the table has no more are dropped and named", "[message]") {
    MessageDraft draft;
    static_cast<void>(draft.setRead(makeTableOf({"a", "b"})));
    draft.setText(1, Language::English, {TextPiece{.text = "b2"}});

    const std::vector<toki::message::MessageKey> dropped = draft.setRead(makeTableOf({"a"}));
    REQUIRE(dropped.size() == 1);
    CHECK(dropped[0].id == 2);
    CHECK_FALSE(draft.isEdited());
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

TEST_CASE("a message key finds the row of the same ID, macro and occurrence", "[message]") {
    MessageTable table = makeTableOf({"a", "b", "c"});
    table.messages[2].id = 1;
    const toki::message::MessageKey key = toki::message::makeMessageKey(table, 2);
    CHECK(key.id == 1);
    CHECK(key.occurrence == 1);
    CHECK(toki::message::findMessage(table, key) == base::usize{2});
    table.messages[2].macro = toki::message::MessageMacro::NesOnly;
    CHECK_FALSE(toki::message::findMessage(table, key).has_value());
}
