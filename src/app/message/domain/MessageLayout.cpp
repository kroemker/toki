#include "MessageLayout.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace toki::message {

namespace {

constexpr u8 Newline = 0x01;
constexpr u8 End = 0x02;
constexpr u8 BoxBreak = 0x04;
constexpr u8 Color = 0x05;
constexpr u8 Shift = 0x06;
constexpr u8 TextId = 0x07;
constexpr u8 QuickTextEnable = 0x08;
constexpr u8 QuickTextDisable = 0x09;
constexpr u8 Persistent = 0x0A;
constexpr u8 Event = 0x0B;
constexpr u8 BoxBreakDelayed = 0x0C;
constexpr u8 AwaitButtonPress = 0x0D;
constexpr u8 Fade = 0x0E;
constexpr u8 Name = 0x0F;
constexpr u8 Ocarina = 0x10;
constexpr u8 Fade2 = 0x11;
constexpr u8 Sfx = 0x12;
constexpr u8 ItemIcon = 0x13;
constexpr u8 TextSpeed = 0x14;
constexpr u8 Background = 0x15;
constexpr u8 MarathonTime = 0x16;
constexpr u8 RaceTime = 0x17;
constexpr u8 Points = 0x18;
constexpr u8 Tokens = 0x19;
constexpr u8 Unskippable = 0x1A;
constexpr u8 TwoChoice = 0x1B;
constexpr u8 ThreeChoice = 0x1C;
constexpr u8 FishInfo = 0x1D;
constexpr u8 HighScore = 0x1E;
constexpr u8 Time = 0x1F;
constexpr u8 Space = 0x20;
constexpr u8 Character = 0xFF;

constexpr u8 BoxTypeNoneBottom = 4;
constexpr u8 BoxTypeNoneNoShadow = 5;
constexpr u8 BoxTypeWooden = 1;
constexpr u8 PositionVariable = 0;
constexpr u8 PositionTop = 1;
constexpr u8 PositionMiddle = 2;

constexpr f32 CharacterScale = 75 / 100.0F;
constexpr s32 WideAdvance = static_cast<s32>(16.0F * (88 / 100.0F));
constexpr s32 SpaceWidth = 6;
constexpr s32 IconIndent = 32;
constexpr s32 ItemIconSize = 32;
constexpr s32 QuestIconSize = 24;
constexpr s32 IconAreaHeight = 44;
constexpr s32 IconTop = 10;
constexpr s32 BackgroundTop = 8;
constexpr s32 EndIconX = 158;
constexpr s32 ChoiceArrowX = 48 + 24;

constexpr std::array NarrowStartOffsets = {26, 20, 16};
constexpr std::array WideStartOffsets = {22, 14};

/// What differs between how `Message_Decode` and `Message_DrawText` lay out
/// English, German and French and how they and `Message_DrawTextWide` lay out
/// Japanese.
struct Script {
    bool isWide = false;
    s32 startX = 0;
    s32 lineSpacing = 0;
    s32 top = 0;
    std::span<const s32> startOffsets;
    s32 itemIconXOffset = 0;
    s32 questIconXOffset = 0;
    std::array<s32, 3> choiceOffsets{};
};

constexpr Script NarrowScript{.startX = TextStartX,
                              .lineSpacing = 12,
                              .top = 8,
                              .startOffsets = NarrowStartOffsets,
                              .itemIconXOffset = 74,
                              .questIconXOffset = 72,
                              .choiceOffsets = {20, 32, 44}};

constexpr Script WideScript{.isWide = true,
                            .startX = WideTextStartX,
                            .lineSpacing = 18,
                            .top = 6,
                            .startOffsets = WideStartOffsets,
                            .itemIconXOffset = 54,
                            .questIconXOffset = 50,
                            .choiceOffsets = {7, 25, 43}};

/// A code of `MESSAGE_WCHAR` text, the code it does what of the narrow text
/// does, and the 16-bit words it takes after it.
struct WideCode {
    u16 wide = 0;
    u8 code = 0;
    usize wordCount = 0;
};

constexpr std::array WideCodes = {
    WideCode{0x000A, Newline, 0},
    WideCode{0x8170, End, 0},
    WideCode{0x81A5, BoxBreak, 0},
    WideCode{0x000B, Color, 1},
    WideCode{0x86C7, Shift, 1},
    WideCode{0x81CB, TextId, 1},
    WideCode{0x8189, QuickTextEnable, 0},
    WideCode{0x818A, QuickTextDisable, 0},
    WideCode{0x86C8, Persistent, 0},
    WideCode{0x819F, Event, 0},
    WideCode{0x81A3, BoxBreakDelayed, 1},
    WideCode{0x81A4, AwaitButtonPress, 0},
    WideCode{0x819E, Fade, 1},
    WideCode{0x874F, Name, 0},
    WideCode{0x81F0, Ocarina, 0},
    WideCode{0x81F4, Fade2, 1},
    WideCode{0x81F3, Sfx, 1},
    WideCode{0x819A, ItemIcon, 1},
    WideCode{0x86C9, TextSpeed, 1},
    WideCode{0x86B3, Background, 2},
    WideCode{0x8791, MarathonTime, 0},
    WideCode{0x8792, RaceTime, 0},
    WideCode{0x879B, Points, 0},
    WideCode{0x86A3, Tokens, 0},
    WideCode{0x8199, Unskippable, 0},
    WideCode{0x81BC, TwoChoice, 0},
    WideCode{0x81B8, ThreeChoice, 0},
    WideCode{0x86A4, FishInfo, 0},
    WideCode{0x869F, HighScore, 1},
    WideCode{0x81A1, Time, 0},
    WideCode{0x8140, Space, 0},
};

constexpr u16 WideEnd = 0x8170;
constexpr u16 WideTouten = 0x8141;
constexpr u16 WideKuten = 0x8142;
constexpr u16 WidePeriod = 0x8144;
constexpr u16 WideNakaten = 0x8145;
constexpr u16 WideQuestionMark = 0x8148;
constexpr u16 WideExclamationMark = 0x8149;
constexpr u16 WideCircumflex = 0x814F;
constexpr u16 WideQuotationRight = 0x8168;
constexpr u16 WideParenthesisLeft = 0x8169;
constexpr u16 WideParenthesisRight = 0x816A;
constexpr u16 WideKagikakkoLeft = 0x8175;
constexpr u16 WideKagikakkoRight = 0x8176;
constexpr u16 WideNumberSign = 0x8194;
constexpr u16 WideAsterisk = 0x8196;
constexpr u16 WideOne = 0x8250;

constexpr std::array<u16, 3> WideNameSample = {0x838A, 0x8393, 0x834E};
constexpr std::array<u16, 6> WideTimerSample = {0x824F, 0x824F, 0x95AA, 0x824F, 0x824F, 0x9562};
constexpr std::array<u16, 1> WideNumberSample = {0x824F};
constexpr std::array<u16, 6> WideTimeSample = {0x8250, 0x8251, 0x8E9E, 0x824F, 0x824F, 0x95AA};

enum class EndType : u8 {
    Default,
    TwoChoice,
    ThreeChoice,
    HasNext,
    Fading,
    Persistent,
    Event,
};

/// The bytes a code takes after it, as `Message_Decode` steps over them.
[[nodiscard]] usize getArgumentCount(const u8 code) {
    switch (code) {
    case Color:
    case Shift:
    case BoxBreakDelayed:
    case Fade:
    case ItemIcon:
    case TextSpeed:
    case HighScore:
        return 1;
    case TextId:
    case Fade2:
    case Sfx:
        return 2;
    case Background:
        return 3;
    default:
        return 0;
    }
}

[[nodiscard]] bool isDecodeEnd(const u8 code) {
    return code == BoxBreak || code == TextId || code == BoxBreakDelayed || code == Event ||
           code == End;
}

[[nodiscard]] std::optional<std::string_view> findSample(const u8 code) {
    switch (code) {
    case Name:
        return "Link";
    case MarathonTime:
    case RaceTime:
        return "00\"00\"";
    case Points:
    case Tokens:
    case FishInfo:
    case HighScore:
        return "0";
    case Time:
        return "12:00";
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::span<const u16> findWideSample(const u8 code) {
    switch (code) {
    case Name:
        return WideNameSample;
    case MarathonTime:
    case RaceTime:
        return WideTimerSample;
    case Points:
    case Tokens:
    case FishInfo:
    case HighScore:
        return WideNumberSample;
    case Time:
        return WideTimeSample;
    default:
        return {};
    }
}

[[nodiscard]] TextColor findDefaultColor(const u8 boxType) {
    return boxType == BoxTypeNoneNoShadow ? TextColor{0, 0, 0} : TextColor{255, 255, 255};
}

/// `Message_SetTextColor`.
[[nodiscard]] TextColor findTextColor(const u8 boxType, const u8 color) {
    const bool isWooden = boxType == BoxTypeWooden;
    switch (color) {
    case 1:
        return isWooden ? TextColor{255, 120, 0} : TextColor{255, 60, 60};
    case 2:
        return TextColor{70, 255, 80};
    case 3:
        return isWooden ? TextColor{80, 110, 255} : TextColor{80, 90, 255};
    case 4:
        if (isWooden) {
            return TextColor{90, 180, 255};
        }
        return boxType == BoxTypeNoneNoShadow ? TextColor{80, 150, 180} : TextColor{100, 180, 255};
    case 5:
        return isWooden ? TextColor{210, 100, 255} : TextColor{255, 150, 180};
    case 6:
        return isWooden ? TextColor{255, 255, 30} : TextColor{225, 255, 50};
    case 7:
        return TextColor{0, 0, 0};
    default:
        return findDefaultColor(boxType);
    }
}

/// How far `Message_DrawTextWide` moves left before drawing `character`.
[[nodiscard]] s32 findWideKerning(const u16 character) {
    switch (character) {
    case WideParenthesisLeft:
    case WideKagikakkoLeft:
        return -6;
    case WideNakaten:
        return -3;
    case WideQuestionMark:
    case WideExclamationMark:
    case WideCircumflex:
    case WideOne:
        return -2;
    default:
        return 0;
    }
}

/// How far `Message_DrawTextWide` moves right after drawing `character`.
[[nodiscard]] s32 findWideAdvance(const u16 character) {
    switch (character) {
    case WidePeriod:
        return 3;
    case WideParenthesisRight:
    case WideKagikakkoRight:
        return 5;
    case WideTouten:
    case WideKuten:
    case WideQuotationRight:
        return 7;
    case WideCircumflex:
    case WideNumberSign:
    case WideAsterisk:
        return 9;
    case WideNakaten:
        return 10;
    default:
        return WideAdvance;
    }
}

/// One entry of the decoded buffer: a code with its arguments, as the narrow
/// text has them, or a character, which may stand in for what the game fills
/// in.
struct Decoded {
    u8 code = 0;
    u16 character = 0;
    std::array<u8, 3> arguments{};
    bool isSample = false;
};

struct DecodedBox {
    std::vector<Decoded> entries;
    usize end = 0;
    usize lineCount = 0;
    u8 choiceCount = 0;
};

class BoxLayouter {
public:
    BoxLayouter(std::span<const u8> bytes, const MessageGeometry& geometry,
                const LayoutRequest& request, const Script& script, MessageLayout& layout)
        : bytes(bytes), geometry(geometry), request(request), script(script), layout(layout) {}

    /// Lays out the box starting at `begin`, and says where the next starts.
    [[nodiscard]] std::optional<usize> layOut(usize begin);

private:
    [[nodiscard]] DecodedBox decode(usize begin) const;
    [[nodiscard]] DecodedBox decodeWide(usize begin) const;
    void count(const Decoded& entry, DecodedBox& decoded) const;
    void draw(const DecodedBox& decoded, LaidBox& box);
    void drawCharacter(u16 character, bool isSample, LaidBox& box);
    void finish(const Decoded& closing, EndType endType, LaidBox& box) const;
    [[nodiscard]] u8 readByte(usize at) const;
    [[nodiscard]] u16 readWord(usize at) const;
    [[nodiscard]] bool isEndAt(usize at) const;
    void report(usize at, std::string message) const;

    std::span<const u8> bytes;
    const MessageGeometry& geometry;
    const LayoutRequest& request;
    const Script& script;
    MessageLayout& layout;
    s32 x = 0;
    s32 y = 0;
    TextColor color;
};

u8 BoxLayouter::readByte(const usize at) const {
    return at < bytes.size() ? bytes[at] : End;
}

u16 BoxLayouter::readWord(const usize at) const {
    if (at + 1 >= bytes.size()) {
        return WideEnd;
    }
    return static_cast<u16>((bytes[at] << 8U) | bytes[at + 1]);
}

bool BoxLayouter::isEndAt(const usize at) const {
    return at >= bytes.size() || (script.isWide ? readWord(at) == WideEnd : readByte(at) == End);
}

void BoxLayouter::report(const usize at, std::string message) const {
    layout.diagnostics.push_back(
        base::makeWarning({}, std::format("byte {}: {}", at, std::move(message))));
}

void BoxLayouter::count(const Decoded& entry, DecodedBox& decoded) const {
    if (entry.code == Newline) {
        ++decoded.lineCount;
    } else if (entry.code == Background) {
        decoded.lineCount = 2;
    } else if (entry.code == ItemIcon) {
        decoded.choiceCount = 1;
    } else if (entry.code == TwoChoice) {
        decoded.choiceCount = 2;
    } else if (entry.code == ThreeChoice) {
        decoded.choiceCount = 3;
    }
}

DecodedBox BoxLayouter::decode(const usize begin) const {
    DecodedBox decoded;
    usize at = begin;
    for (;;) {
        Decoded entry{.code = readByte(at)};
        const usize argumentCount = getArgumentCount(entry.code);
        for (usize index = 0; index < argumentCount; ++index) {
            entry.arguments[index] = readByte(at + 1 + index);
        }
        if (isDecodeEnd(entry.code)) {
            decoded.entries.push_back(entry);
            decoded.end = at + 1 + argumentCount;
            return decoded;
        }
        if (const std::optional<std::string_view> sample = findSample(entry.code)) {
            for (const char character : *sample) {
                decoded.entries.push_back(Decoded{
                    .code = Character, .character = static_cast<u8>(character), .isSample = true});
            }
        } else if (entry.code > Space) {
            decoded.entries.push_back(Decoded{.code = Character, .character = entry.code});
        } else {
            count(entry, decoded);
            decoded.entries.push_back(entry);
        }
        at += 1 + argumentCount;
    }
}

DecodedBox BoxLayouter::decodeWide(const usize begin) const {
    DecodedBox decoded;
    usize at = begin;
    for (;;) {
        const u16 word = readWord(at);
        const auto found = std::ranges::find(WideCodes, word, &WideCode::wide);
        if (found == WideCodes.end()) {
            decoded.entries.push_back(Decoded{.code = Character, .character = word});
            at += 2;
            continue;
        }
        Decoded entry{.code = found->code};
        const u16 first = readWord(at + 2);
        if (found->wordCount == 2) {
            const u16 second = readWord(at + 4);
            entry.arguments = {static_cast<u8>(first & 0xFFU), static_cast<u8>(second >> 8U),
                               static_cast<u8>(second & 0xFFU)};
        } else if (getArgumentCount(entry.code) == 2) {
            entry.arguments = {static_cast<u8>(first >> 8U), static_cast<u8>(first & 0xFFU), 0};
        } else if (found->wordCount == 1) {
            entry.arguments = {static_cast<u8>(first & 0xFFU), 0, 0};
        }
        const usize next = at + 2 * (1 + found->wordCount);
        if (isDecodeEnd(entry.code)) {
            decoded.entries.push_back(entry);
            decoded.end = next;
            return decoded;
        }
        const std::span<const u16> sample = findWideSample(entry.code);
        if (!sample.empty()) {
            for (const u16 character : sample) {
                decoded.entries.push_back(
                    Decoded{.code = Character, .character = character, .isSample = true});
            }
        } else {
            count(entry, decoded);
            decoded.entries.push_back(entry);
        }
        at = next;
    }
}

std::optional<usize> BoxLayouter::layOut(const usize begin) {
    const DecodedBox decoded = script.isWide ? decodeWide(begin) : decode(begin);
    const u8 boxType = request.boxType < BoxTypeCount ? request.boxType : 0;
    const BoxPlacement& placement = geometry.boxes[boxType];
    LaidBox box{.begin = begin,
                .end = decoded.end,
                .boxType = boxType,
                .x = placement.x,
                .isWide = script.isWide,
                .isPositionVariable = request.boxPosition == PositionVariable};
    box.y = request.boxPosition == PositionTop      ? placement.upperY
            : request.boxPosition == PositionMiddle ? placement.middleY
                                                    : placement.lowerY;

    draw(decoded, box);
    layout.boxes.push_back(std::move(box));

    const Decoded& closing = decoded.entries.back();
    if (closing.code == End || isEndAt(decoded.end)) {
        return std::nullopt;
    }
    return decoded.end;
}

void BoxLayouter::draw(const DecodedBox& decoded, LaidBox& box) {
    s32 startY = box.y + script.top;
    if (box.boxType != BoxTypeNoneBottom && decoded.lineCount < script.startOffsets.size()) {
        startY = box.y + script.startOffsets[decoded.lineCount];
    }
    const s32 startX = script.startX + (script.isWide && decoded.choiceCount == 3 ? IconIndent : 0);
    x = startX;
    y = startY;
    color = findDefaultColor(box.boxType);
    box.lines.push_back(LaidLine{.y = y, .startX = x, .endX = x});
    EndType endType = EndType::Default;

    for (const Decoded& entry : decoded.entries) {
        switch (entry.code) {
        case Character:
            drawCharacter(entry.character, entry.isSample, box);
            break;
        case Newline:
            x = startX;
            if (script.isWide ? decoded.choiceCount == 1 || decoded.choiceCount == 2
                              : decoded.choiceCount == 1 || decoded.choiceCount == 3 ||
                                    (decoded.choiceCount == 2 && y != startY)) {
                x += IconIndent;
            }
            y += script.lineSpacing;
            box.lines.push_back(LaidLine{.y = y, .startX = x, .endX = x});
            break;
        case Color:
            color = findTextColor(box.boxType, entry.arguments[0] & 0xF);
            break;
        case Space:
            x += SpaceWidth;
            box.lines.back().endX = x;
            break;
        case Shift:
            x += entry.arguments[0];
            box.lines.back().endX = x;
            break;
        case ItemIcon: {
            const bool isQuest = entry.arguments[0] >= request.firstQuestItem;
            const s32 size = isQuest ? QuestIconSize : ItemIconSize;
            box.icon = LaidIcon{
                .item = entry.arguments[0],
                .isQuest = isQuest,
                .textureIndex = static_cast<u32>(
                    isQuest ? entry.arguments[0] - request.firstQuestItem : entry.arguments[0]),
                .x = x + script.startX -
                     (isQuest ? script.questIconXOffset : script.itemIconXOffset),
                .y = box.y + IconTop + (IconAreaHeight - size) / 2,
                .size = size};
            x += IconIndent;
            box.lines.back().endX = x;
            break;
        }
        case Background:
            box.background = LaidBackground{.picture = entry.arguments[0],
                                            .foreColor = static_cast<u8>(entry.arguments[1] >> 4),
                                            .backColor = static_cast<u8>(entry.arguments[1] & 0xF),
                                            .yOffset = static_cast<u8>(entry.arguments[2] >> 4),
                                            .x = x,
                                            .y = box.y + BackgroundTop};
            x += IconIndent;
            break;
        case QuickTextEnable:
        case QuickTextDisable:
        case AwaitButtonPress:
        case Ocarina:
        case Unskippable:
        case Sfx:
        case TextSpeed:
            break;
        case TwoChoice:
            endType = EndType::TwoChoice;
            break;
        case ThreeChoice:
            endType = EndType::ThreeChoice;
            break;
        case TextId:
            finish(entry, EndType::HasNext, box);
            return;
        case Fade:
        case Fade2:
            finish(entry, EndType::Fading, box);
            return;
        case Persistent:
            finish(entry, EndType::Persistent, box);
            return;
        case Event:
            finish(entry, EndType::Event, box);
            return;
        case BoxBreak:
        case BoxBreakDelayed:
        case End:
            finish(entry, endType, box);
            return;
        default:
            report(box.begin, std::format("0x{:02X} is no code the game knows", entry.code));
            break;
        }
    }
}

void BoxLayouter::drawCharacter(const u16 character, const bool isSample, LaidBox& box) {
    constexpr u32 GlyphSize = 128;
    if (script.isWide) {
        const std::optional<u32> offset = findKanjiOffset(character, geometry);
        if (!offset) {
            report(box.begin, std::format("0x{:04X} has no glyph in the kanji font", character));
            return;
        }
        x += findWideKerning(character);
        box.characters.push_back(LaidCharacter{.character = character,
                                               .fontOffset = *offset,
                                               .x = x,
                                               .y = y,
                                               .color = color,
                                               .isSample = isSample});
        x += findWideAdvance(character);
        box.lines.back().endX = x;
        return;
    }
    const usize index = static_cast<usize>(character - Space);
    if (character < Space || index >= FontCharacterCount) {
        report(box.begin, std::format("0x{:02X} is past the font's last character", character));
        return;
    }
    box.characters.push_back(LaidCharacter{.character = character,
                                           .fontOffset = static_cast<u32>(index) * GlyphSize,
                                           .x = x,
                                           .y = y,
                                           .color = color,
                                           .isSample = isSample});
    x += static_cast<s32>(geometry.fontWidths[index] * CharacterScale);
    box.lines.back().endX = x;
}

void BoxLayouter::finish(const Decoded& closing, const EndType endType, LaidBox& box) const {
    if (endType == EndType::TwoChoice || endType == EndType::ThreeChoice) {
        const usize first = endType == EndType::TwoChoice ? 1 : 0;
        for (usize choice = first; choice < script.choiceOffsets.size(); ++choice) {
            box.choiceYs.push_back(box.y + script.choiceOffsets[choice]);
        }
    }
    box.endIconX = EndIconX;
    box.endIconY = box.y + geometry.boxes[box.boxType].endIconOffset;
    switch (closing.code) {
    case BoxBreak:
        box.endIcon = EndIcon::Triangle;
        return;
    case BoxBreakDelayed:
    case Fade:
    case Fade2:
    case Persistent:
        box.endIcon = EndIcon::None;
        return;
    default:
        break;
    }
    switch (endType) {
    case EndType::TwoChoice:
    case EndType::ThreeChoice:
        box.endIcon = EndIcon::Arrow;
        box.endIconX = ChoiceArrowX;
        box.endIconY = box.choiceYs.front();
        return;
    case EndType::Default:
        box.endIcon = closing.code == End ? EndIcon::Square : EndIcon::Triangle;
        return;
    case EndType::HasNext:
    case EndType::Event:
        box.endIcon = EndIcon::Triangle;
        return;
    case EndType::Fading:
    case EndType::Persistent:
        box.endIcon = EndIcon::None;
        return;
    }
}

[[nodiscard]] MessageLayout layOutBoxes(const std::span<const u8> bytes,
                                        const MessageGeometry& geometry,
                                        const LayoutRequest& request, const Script& script) {
    MessageLayout layout;
    BoxLayouter layouter(bytes, geometry, request, script, layout);
    std::optional<usize> begin = 0;
    while (begin) {
        begin = layouter.layOut(*begin);
    }
    return layout;
}

} // namespace

s32 findLineStart(const LaidBox& box) {
    return box.isWide ? WideTextStartX : TextStartX;
}

s32 findLineLimit(const LaidBox& box) {
    return box.x + BoxWidth - (findLineStart(box) - box.x);
}

MessageLayout layoutMessage(const std::span<const u8> bytes, const MessageGeometry& geometry,
                            const LayoutRequest& request) {
    return layOutBoxes(bytes, geometry, request, NarrowScript);
}

MessageLayout layoutWideMessage(const std::span<const u8> bytes, const MessageGeometry& geometry,
                                const LayoutRequest& request) {
    return layOutBoxes(bytes, geometry, request, WideScript);
}

} // namespace toki::message
