#include "MessageCodes.h"

#include "app/message/domain/MessageMarkup.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

namespace toki::message {

namespace {

constexpr std::array ShiftArguments = {CodeArgument{.label = "Pixels", .initial = "8"}};
constexpr std::array SpeedArguments = {CodeArgument{.label = "Frames", .initial = "1"}};
constexpr std::array FramesArguments = {CodeArgument{.label = "Frames", .initial = "20"}};
constexpr std::array LongFramesArguments = {
    CodeArgument{.label = "Frames", .initial = "20", .maximum = 0xFFFF}};
constexpr std::array TextIdArguments = {
    CodeArgument{.label = "Message", .kind = ArgumentKind::MessageId, .initial = "0x0000"}};
constexpr std::array SoundArguments = {CodeArgument{.label = "Sound", .kind = ArgumentKind::Sound}};
constexpr std::array ItemArguments = {CodeArgument{.label = "Item", .kind = ArgumentKind::Item}};
constexpr std::array HighScoreArguments = {
    CodeArgument{.label = "Score", .kind = ArgumentKind::HighScore}};
constexpr std::array BackgroundArguments = {CodeArgument{.label = "First", .initial = "0"},
                                            CodeArgument{.label = "Second", .initial = "0"},
                                            CodeArgument{.label = "Third", .initial = "0"}};

constexpr std::array InsertableCodes = {
    InsertableCode{.macro = "NAME"},
    InsertableCode{.macro = "TWO_CHOICE"},
    InsertableCode{.macro = "THREE_CHOICE"},
    InsertableCode{.macro = "UNSKIPPABLE"},
    InsertableCode{.macro = "SHIFT", .arguments = ShiftArguments},
    InsertableCode{.macro = "TEXT_SPEED", .arguments = SpeedArguments},
    InsertableCode{.macro = "QUICKTEXT_ENABLE"},
    InsertableCode{.macro = "QUICKTEXT_DISABLE"},
    InsertableCode{.macro = "AWAIT_BUTTON_PRESS"},
    InsertableCode{.macro = "BOX_BREAK_DELAYED", .arguments = FramesArguments},
    InsertableCode{.macro = "FADE", .arguments = FramesArguments},
    InsertableCode{.macro = "FADE2", .arguments = LongFramesArguments},
    InsertableCode{.macro = "PERSISTENT"},
    InsertableCode{.macro = "EVENT"},
    InsertableCode{.macro = "TEXTID", .arguments = TextIdArguments},
    InsertableCode{.macro = "SFX", .arguments = SoundArguments},
    InsertableCode{.macro = "ITEM_ICON", .arguments = ItemArguments},
    InsertableCode{.macro = "OCARINA"},
    InsertableCode{.macro = "BACKGROUND", .arguments = BackgroundArguments},
    InsertableCode{.macro = "MARATHON_TIME"},
    InsertableCode{.macro = "RACE_TIME"},
    InsertableCode{.macro = "POINTS"},
    InsertableCode{.macro = "TOKENS"},
    InsertableCode{.macro = "FISH_INFO"},
    InsertableCode{.macro = "HIGHSCORE", .arguments = HighScoreArguments},
    InsertableCode{.macro = "TIME"},
};

[[nodiscard]] std::string makeWordsOf(const CodePiece& code) {
    const std::array<Piece, 1> pieces = {code};
    const std::string markup = formatMarkup(pieces);
    return markup.substr(1, markup.size() - 2);
}

} // namespace

std::span<const InsertableCode> listInsertableCodes() {
    return InsertableCodes;
}

const InsertableCode* findInsertableCode(const std::string_view macro) {
    const auto found = std::ranges::find(InsertableCodes, macro, &InsertableCode::macro);
    return found != InsertableCodes.end() ? &*found : nullptr;
}

CodePiece makeInitialCode(const InsertableCode& code) {
    CodePiece made{.name = std::string(code.macro)};
    for (const CodeArgument& argument : code.arguments) {
        made.arguments.emplace_back(argument.initial);
    }
    return made;
}

std::string makeCodeName(const CodePiece& code) {
    if (code.name == "COLOR") {
        return std::format("{} Text", makeWordsOf(code));
    }
    return makeWordsOf(CodePiece{.name = code.name});
}

} // namespace toki::message
