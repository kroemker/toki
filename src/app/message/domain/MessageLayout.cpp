#include "MessageLayout.h"

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

constexpr u8 BoxTypeNoneBottom = 4;
constexpr u8 BoxTypeNoneNoShadow = 5;
constexpr u8 BoxTypeWooden = 1;
constexpr u8 PositionVariable = 0;
constexpr u8 PositionTop = 1;
constexpr u8 PositionMiddle = 2;

constexpr s32 TextStartX = 65;
constexpr s32 LineSpacing = 12;
constexpr f32 CharacterScale = 75 / 100.0F;
constexpr s32 SpaceWidth = 6;
constexpr s32 IconIndent = 32;
constexpr s32 ItemIconXOffset = 74;
constexpr s32 QuestIconXOffset = 72;
constexpr s32 ItemIconSize = 32;
constexpr s32 QuestIconSize = 24;
constexpr s32 IconAreaHeight = 44;
constexpr s32 IconTop = 10;
constexpr s32 BackgroundTop = 8;
constexpr s32 EndIconX = 158;
constexpr s32 ChoiceArrowX = 48 + 24;
constexpr std::array ChoiceOffsets = {20, 32, 44};

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

/// One entry of the decoded buffer: a code with its arguments, or a
/// character, which may stand in for what the game fills in.
struct Decoded {
    u8 code = 0;
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
                const LayoutRequest& request, MessageLayout& layout)
        : bytes(bytes), geometry(geometry), request(request), layout(layout) {}

    /// Lays out the box starting at `begin`, and says where the next starts.
    [[nodiscard]] std::optional<usize> layOut(usize begin);

private:
    [[nodiscard]] DecodedBox decode(usize begin) const;
    void draw(const DecodedBox& decoded, LaidBox& box);
    void drawCharacter(u8 character, bool isSample, LaidBox& box);
    void finish(const Decoded& closing, EndType endType, LaidBox& box) const;
    [[nodiscard]] u8 readByte(usize at) const;
    void report(usize at, std::string message) const;

    std::span<const u8> bytes;
    const MessageGeometry& geometry;
    const LayoutRequest& request;
    MessageLayout& layout;
    s32 x = 0;
    s32 y = 0;
    TextColor color;
};

u8 BoxLayouter::readByte(const usize at) const {
    return at < bytes.size() ? bytes[at] : End;
}

void BoxLayouter::report(const usize at, std::string message) const {
    layout.diagnostics.push_back(
        base::makeWarning({}, std::format("byte {}: {}", at, std::move(message))));
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
                decoded.entries.push_back(
                    Decoded{.code = static_cast<u8>(character), .isSample = true});
            }
        } else {
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
            decoded.entries.push_back(entry);
        }
        at += 1 + argumentCount;
    }
}

std::optional<usize> BoxLayouter::layOut(const usize begin) {
    const DecodedBox decoded = decode(begin);
    const u8 boxType = request.boxType < BoxTypeCount ? request.boxType : 0;
    const BoxPlacement& placement = geometry.boxes[boxType];
    LaidBox box{.begin = begin,
                .end = decoded.end,
                .boxType = boxType,
                .x = placement.x,
                .isPositionVariable = request.boxPosition == PositionVariable};
    box.y = request.boxPosition == PositionTop      ? placement.upperY
            : request.boxPosition == PositionMiddle ? placement.middleY
                                                    : placement.lowerY;

    draw(decoded, box);
    layout.boxes.push_back(std::move(box));

    const Decoded& closing = decoded.entries.back();
    if (closing.code == End || decoded.end >= bytes.size() || readByte(decoded.end) == End) {
        return std::nullopt;
    }
    return decoded.end;
}

void BoxLayouter::draw(const DecodedBox& decoded, LaidBox& box) {
    s32 startY = box.y + 8;
    if (box.boxType != BoxTypeNoneBottom) {
        constexpr std::array StartOffsets = {26, 20, 16};
        if (decoded.lineCount < StartOffsets.size()) {
            startY = box.y + StartOffsets[decoded.lineCount];
        }
    }
    x = TextStartX;
    y = startY;
    color = findDefaultColor(box.boxType);
    box.lines.push_back(LaidLine{.y = y, .startX = x, .endX = x});
    EndType endType = EndType::Default;

    for (usize index = 0; index < decoded.entries.size(); ++index) {
        const Decoded& entry = decoded.entries[index];
        if (entry.isSample) {
            drawCharacter(entry.code, true, box);
            continue;
        }
        switch (entry.code) {
        case Newline:
            x = TextStartX;
            if (decoded.choiceCount == 1 || decoded.choiceCount == 3 ||
                (decoded.choiceCount == 2 && y != startY)) {
                x += IconIndent;
            }
            y += LineSpacing;
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
            box.icon =
                LaidIcon{.item = entry.arguments[0],
                         .x = x + TextStartX - (isQuest ? QuestIconXOffset : ItemIconXOffset),
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
            endType = EndType::HasNext;
            finish(entry, endType, box);
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
            if (entry.code > Space) {
                drawCharacter(entry.code, false, box);
            } else {
                report(box.begin, std::format("0x{:02X} is no code the game knows", entry.code));
            }
            break;
        }
    }
}

void BoxLayouter::drawCharacter(const u8 character, const bool isSample, LaidBox& box) {
    const usize index = static_cast<usize>(character - Space);
    if (index >= FontCharacterCount) {
        report(box.begin, std::format("0x{:02X} is past the font's last character", character));
        return;
    }
    box.characters.push_back(LaidCharacter{
        .character = character, .x = x, .y = y, .color = color, .isSample = isSample});
    x += static_cast<s32>(geometry.fontWidths[index] * CharacterScale);
    box.lines.back().endX = x;
}

void BoxLayouter::finish(const Decoded& closing, const EndType endType, LaidBox& box) const {
    if (endType == EndType::TwoChoice || endType == EndType::ThreeChoice) {
        const usize first = endType == EndType::TwoChoice ? 1 : 0;
        for (usize choice = first; choice < ChoiceOffsets.size(); ++choice) {
            box.choiceYs.push_back(box.y + ChoiceOffsets[choice]);
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

} // namespace

MessageLayout layoutMessage(const std::span<const u8> bytes, const MessageGeometry& geometry,
                            const LayoutRequest& request) {
    MessageLayout layout;
    BoxLayouter layouter(bytes, geometry, request, layout);
    std::optional<usize> begin = 0;
    while (begin) {
        begin = layouter.layOut(*begin);
    }
    return layout;
}

} // namespace toki::message
