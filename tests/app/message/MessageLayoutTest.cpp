#include "app/message/domain/MessageLayout.h"

#include "app/message/domain/MessageGeometry.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

using base::s32;
using base::u8;
using toki::message::EndIcon;
using toki::message::LayoutRequest;
using toki::message::MessageGeometry;
using toki::message::MessageLayout;

namespace {

constexpr u8 Newline = 0x01;
constexpr u8 End = 0x02;
constexpr u8 BoxBreak = 0x04;
constexpr u8 Color = 0x05;
constexpr u8 Name = 0x0F;
constexpr u8 ItemIcon = 0x13;
constexpr u8 TwoChoice = 0x1B;
constexpr u8 Fade = 0x0E;

constexpr u8 TypeBlack = 0;
constexpr u8 TypeWooden = 1;
constexpr u8 TypeNoneBottom = 4;
constexpr u8 PositionVariable = 0;
constexpr u8 PositionTop = 1;
constexpr u8 PositionBottom = 3;
constexpr s32 QuestItem = 0x66;

[[nodiscard]] MessageGeometry makeGeometry() {
    MessageGeometry geometry;
    geometry.fontWidths.fill(8.0F);
    geometry.fontWidths['W' - ' '] = 15.0F;
    for (toki::message::BoxPlacement& box : geometry.boxes) {
        box = {.x = 34, .lowerY = 142, .upperY = 38, .middleY = 90, .endIconOffset = 59};
    }
    geometry.boxes[TypeNoneBottom] = {
        .x = 34, .lowerY = 174, .upperY = 174, .middleY = 174, .endIconOffset = 34};
    return geometry;
}

class Bytes {
public:
    Bytes& add(const std::string_view text) {
        bytes.insert(bytes.end(), text.begin(), text.end());
        return *this;
    }

    Bytes& add(const u8 code) {
        bytes.push_back(code);
        return *this;
    }

    [[nodiscard]] const std::vector<u8>& get() const {
        return bytes;
    }

private:
    std::vector<u8> bytes;
};

[[nodiscard]] MessageLayout layOut(const Bytes& bytes, const u8 boxType = TypeBlack,
                                   const u8 boxPosition = PositionBottom) {
    return toki::message::layoutMessage(
        bytes.get(), makeGeometry(),
        LayoutRequest{.boxType = boxType, .boxPosition = boxPosition, .firstQuestItem = QuestItem});
}

} // namespace

TEST_CASE("one line stands in the middle of its box, each character advancing by three "
          "quarters of its width",
          "[message]") {
    const MessageLayout layout = layOut(Bytes().add("AW ").add("B").add(End));

    REQUIRE(layout.diagnostics.empty());
    REQUIRE(layout.boxes.size() == 1);
    const toki::message::LaidBox& box = layout.boxes[0];
    CHECK(box.x == 34);
    CHECK(box.y == 142);
    REQUIRE(box.characters.size() == 3);
    CHECK(box.characters[0].x == 65);
    CHECK(box.characters[0].y == 142 + 26);
    CHECK(box.characters[1].x == 65 + 6);
    CHECK(box.characters[2].x == 65 + 6 + 11 + 6);
    REQUIRE(box.lines.size() == 1);
    CHECK(box.lines[0].endX == 65 + 6 + 11 + 6 + 6);
    CHECK(box.endIcon == EndIcon::Square);
    CHECK(box.endIconX == 158);
    CHECK(box.endIconY == 142 + 59);
}

TEST_CASE("a line may reach as far from the box's right edge as text starts from its left",
          "[message]") {
    const MessageLayout layout = layOut(Bytes().add("A").add(End));

    REQUIRE(layout.boxes.size() == 1);
    CHECK(toki::message::findLineLimit(layout.boxes[0]) == 34 + 256 - (65 - 34));
}

TEST_CASE("more lines start higher, and four fill the box from its top", "[message]") {
    const MessageLayout two = layOut(Bytes().add("A").add(Newline).add("B").add(End));
    REQUIRE(two.boxes.size() == 1);
    CHECK(two.boxes[0].characters[0].y == 142 + 20);
    CHECK(two.boxes[0].characters[1].y == 142 + 20 + 12);
    CHECK(two.boxes[0].characters[1].x == 65);

    const MessageLayout four = layOut(
        Bytes().add("A").add(Newline).add("B").add(Newline).add("C").add(Newline).add("D").add(
            End));
    CHECK(four.boxes[0].characters[0].y == 142 + 8);
    CHECK(four.boxes[0].lines.size() == 4);
}

TEST_CASE("a box without a frame starts its text at the top whatever its lines", "[message]") {
    const MessageLayout layout = layOut(Bytes().add("A").add(End), TypeNoneBottom);

    CHECK(layout.boxes[0].y == 174);
    CHECK(layout.boxes[0].characters[0].y == 174 + 8);
    CHECK(layout.boxes[0].endIconY == 174 + 34);
}

TEST_CASE("a box break starts the next box, and the first waits on a triangle", "[message]") {
    const MessageLayout layout =
        layOut(Bytes().add("A").add(BoxBreak).add("B").add(End), TypeBlack, PositionTop);

    REQUIRE(layout.boxes.size() == 2);
    CHECK(layout.boxes[0].endIcon == EndIcon::Triangle);
    CHECK(layout.boxes[0].y == 38);
    CHECK(layout.boxes[1].begin == 2);
    CHECK(layout.boxes[1].characters[0].character == 'B');
    CHECK(layout.boxes[1].endIcon == EndIcon::Square);
}

TEST_CASE("colors follow the box type, as Message_SetTextColor picks them", "[message]") {
    const Bytes bytes =
        Bytes().add(Color).add(0x41).add("A").add(Color).add(0x40).add("B").add(End);

    const MessageLayout black = layOut(bytes);
    CHECK(black.boxes[0].characters[0].color == toki::message::TextColor{255, 60, 60});
    CHECK(black.boxes[0].characters[1].color == toki::message::TextColor{255, 255, 255});
    const MessageLayout wooden = layOut(bytes, TypeWooden);
    CHECK(wooden.boxes[0].characters[0].color == toki::message::TextColor{255, 120, 0});
}

TEST_CASE("an item icon stands left of the text, which every line indents past", "[message]") {
    const MessageLayout item =
        layOut(Bytes().add(ItemIcon).add(0x10).add("A").add(Newline).add("B").add(End));
    const toki::message::LaidBox& box = item.boxes[0];

    REQUIRE(box.icon.has_value());
    CHECK(box.icon->size == 32);
    CHECK(box.icon->x == 65 + 65 - 74);
    CHECK(box.icon->y == 142 + 10 + 6);
    CHECK(box.characters[0].x == 65 + 32);
    CHECK(box.characters[1].x == 65 + 32);

    const MessageLayout quest = layOut(Bytes().add(ItemIcon).add(QuestItem).add("A").add(End));
    CHECK(quest.boxes[0].icon->size == 24);
    CHECK(quest.boxes[0].icon->x == 65 + 65 - 72);
}

TEST_CASE("a choice indents the lines after the first and ends on an arrow", "[message]") {
    const MessageLayout layout = layOut(Bytes()
                                            .add("Q")
                                            .add(Newline)
                                            .add(Newline)
                                            .add(TwoChoice)
                                            .add("Y")
                                            .add(Newline)
                                            .add("N")
                                            .add(End));
    const toki::message::LaidBox& box = layout.boxes[0];

    REQUIRE(box.characters.size() == 3);
    CHECK(box.characters[0].x == 65);
    CHECK(box.characters[1].x == 65 + 32);
    CHECK(box.characters[1].y == 142 + 32);
    CHECK(box.characters[2].x == 65 + 32);
    CHECK(box.endIcon == EndIcon::Arrow);
    CHECK(box.endIconX == 72);
    CHECK(box.choiceYs == std::vector<s32>{142 + 32, 142 + 44});
}

TEST_CASE("the player's name is a sample, and a variable box is laid out below", "[message]") {
    const MessageLayout layout =
        layOut(Bytes().add(Name).add("!").add(End), TypeBlack, PositionVariable);
    const toki::message::LaidBox& box = layout.boxes[0];

    CHECK(box.isPositionVariable);
    CHECK(box.y == 142);
    REQUIRE(box.characters.size() == 5);
    CHECK(box.characters[0].isSample);
    CHECK_FALSE(box.characters[4].isSample);
}

TEST_CASE("every code and character has a stop where the pen stood, a sample one where it "
          "starts",
          "[message]") {
    const MessageLayout layout =
        layOut(Bytes().add("A").add(Color).add(0x41).add(Name).add(Newline).add("B").add(End));

    REQUIRE(layout.boxes.size() == 1);
    const toki::message::LaidBox& box = layout.boxes[0];
    REQUIRE(box.stops.size() == 6);
    CHECK(box.stops[0].byte == 0);
    CHECK(box.stops[0].x == 65);
    CHECK(box.stops[1].byte == 1);
    CHECK(box.stops[1].x == 65 + 6);
    CHECK(box.stops[1].color == toki::message::TextColor{255, 60, 60});
    CHECK(box.stops[2].byte == 3);
    CHECK(box.stops[2].x == 65 + 6);
    CHECK(box.stops[3].byte == 4);
    CHECK(box.stops[3].x == 65 + 6 + 4 * 6);
    CHECK(box.stops[4].byte == 5);
    CHECK(box.stops[4].x == 65);
    CHECK(box.stops[4].y == box.stops[3].y + 12);
    CHECK(box.stops[5].byte == 6);
    CHECK(box.characters[1].byte == 3);
    CHECK(box.characters[4].byte == 3);
    CHECK(box.characters[5].byte == 5);
}

TEST_CASE("text after a fade is never drawn and the box shows no icon", "[message]") {
    const MessageLayout layout = layOut(Bytes().add("A").add(Fade).add(0x20).add("B").add(End));

    REQUIRE(layout.boxes.size() == 1);
    CHECK(layout.boxes[0].characters.size() == 1);
    CHECK(layout.boxes[0].endIcon == EndIcon::None);
}

TEST_CASE("a byte the game has no code or character for is a diagnostic", "[message]") {
    const MessageLayout layout = layOut(Bytes().add(0x03).add("A").add(End));

    CHECK(layout.diagnostics.size() == 1);
    CHECK(layout.boxes[0].characters.size() == 1);
}

namespace {

[[nodiscard]] std::vector<u8> makeWide(const std::vector<base::u16>& words) {
    std::vector<u8> bytes;
    for (const base::u16 word : words) {
        bytes.push_back(static_cast<u8>(word >> 8U));
        bytes.push_back(static_cast<u8>(word & 0xFFU));
    }
    return bytes;
}

[[nodiscard]] MessageGeometry makeWideGeometry() {
    MessageGeometry geometry = makeGeometry();
    geometry.nonKanjiIndices.assign(0xBC * 7, 0);
    for (base::usize index = 0; index < geometry.nonKanjiIndices.size(); ++index) {
        geometry.nonKanjiIndices[index] = static_cast<base::u16>(index);
    }
    return geometry;
}

} // namespace

TEST_CASE("the kanji index table is read from kanread.s", "[message]") {
    constexpr std::string_view Assembly = "DATA(sNonKanjiIndices)\n"
                                          "/* 0x814_ */ .half 0x0000, 0x0001, 0x0002\n"
                                          "/* 0x815_ */ .half 0x0010,0x0011\r\n"
                                          "ENDDATA(sNonKanjiIndices)\n";
    CHECK(toki::message::parseNonKanjiIndices(Assembly) ==
          std::vector<base::u16>{0x0000, 0x0001, 0x0002, 0x0010, 0x0011});
    CHECK(toki::message::parseNonKanjiIndices("no table").empty());
}

TEST_CASE("a kanji is found where Kanji_OffsetFromShiftJIS finds it", "[message]") {
    const MessageGeometry geometry = makeWideGeometry();
    CHECK(toki::message::findKanjiOffset(0x889F, geometry) == (0x30AU + 0x5EU) * 128);
    CHECK(toki::message::findKanjiOffset(0x8980, geometry) == (0x30AU + 0x3FU + 0xBCU) * 128);
    CHECK(toki::message::findKanjiOffset(0x8142, geometry) == 2U * 128);
    CHECK(toki::message::findKanjiOffset(0x82A0, geometry) == (0x5FU + 0xBCU) * 128);
    CHECK_FALSE(toki::message::findKanjiOffset(0x8742, MessageGeometry()).has_value());
}

TEST_CASE("Japanese is laid out two bytes a character, with the spacing of its signs",
          "[message]") {
    const MessageGeometry geometry = makeWideGeometry();
    const std::vector<u8> bytes =
        makeWide({0x82A0, 0x8142, 0x82A0, 0x000A, 0x8175, 0x82A0, 0x8140, 0x82A0, 0x8170});
    const MessageLayout layout = toki::message::layoutWideMessage(
        bytes, geometry, LayoutRequest{.boxType = TypeBlack, .boxPosition = PositionBottom});

    CHECK(layout.diagnostics.empty());
    REQUIRE(layout.boxes.size() == 1);
    const toki::message::LaidBox& box = layout.boxes[0];
    CHECK(box.isWide);
    REQUIRE(box.characters.size() == 6);
    CHECK(box.characters[0].x == 50);
    CHECK(box.characters[0].y == 142 + 14);
    CHECK(box.characters[1].x == 64);
    CHECK(box.characters[2].x == 71);
    CHECK(box.characters[3].x == 44);
    CHECK(box.characters[3].y == 142 + 14 + 18);
    CHECK(box.characters[4].x == 58);
    CHECK(box.characters[5].x == 78);
    CHECK(box.endIcon == EndIcon::Square);
    CHECK(toki::message::findLineLimit(box) == 34 + 256 - (50 - 34));
}

TEST_CASE("Japanese codes take their arguments in words and choices sit higher", "[message]") {
    const MessageGeometry geometry = makeWideGeometry();
    const std::vector<u8> bytes =
        makeWide({0x000B, 0x0C01, 0x82A0, 0x000A, 0x82A0, 0x000A, 0x82A0, 0x81BC, 0x8170});
    const MessageLayout layout = toki::message::layoutWideMessage(
        bytes, geometry, LayoutRequest{.boxType = TypeBlack, .boxPosition = PositionBottom});

    CHECK(layout.diagnostics.empty());
    REQUIRE(layout.boxes.size() == 1);
    const toki::message::LaidBox& box = layout.boxes[0];
    REQUIRE(box.characters.size() == 3);
    CHECK(box.characters[0].color == toki::message::TextColor{255, 60, 60});
    CHECK(box.characters[0].x == 50);
    CHECK(box.characters[1].x == 50 + 32);
    CHECK(box.characters[0].y == 142 + 6);
    CHECK(box.endIcon == EndIcon::Arrow);
    CHECK(box.choiceYs == std::vector<s32>{142 + 25, 142 + 43});
}
