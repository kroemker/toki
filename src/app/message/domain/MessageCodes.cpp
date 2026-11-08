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
    InsertableCode{.macro = "FADE2", .arguments = FramesArguments},
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

[[nodiscard]] std::string findArgument(const CodePiece& code, const usize index) {
    return index < code.arguments.size() ? code.arguments[index] : std::string("?");
}

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

std::string describeCode(const CodePiece& code) {
    const std::string_view name = code.name;
    if (name == "COLOR") {
        return std::format("{} Text", makeWordsOf(code));
    }
    if (name == "SHIFT") {
        return std::format("{} Pixels of Space", findArgument(code, 0));
    }
    if (name == "TEXT_SPEED") {
        return std::format("{} Frames a Letter", findArgument(code, 0));
    }
    if (name == "QUICKTEXT_ENABLE") {
        return "Instant Text";
    }
    if (name == "QUICKTEXT_DISABLE") {
        return "Letter by Letter Text";
    }
    if (name == "AWAIT_BUTTON_PRESS") {
        return "Wait for a Button";
    }
    if (name == "BOX_BREAK_DELAYED") {
        return std::format("Next Box after {} Frames", findArgument(code, 0));
    }
    if (name == "FADE" || name == "FADE2") {
        return std::format("Close after {} Frames", findArgument(code, 0));
    }
    if (name == "PERSISTENT") {
        return "Stays Open";
    }
    if (name == "EVENT") {
        return "Waits for the Game";
    }
    if (name == "TEXTID") {
        return std::format("Next Message {}", findArgument(code, 0));
    }
    if (name == "TWO_CHOICE") {
        return "Two Choices";
    }
    if (name == "THREE_CHOICE") {
        return "Three Choices";
    }
    if (name == "UNSKIPPABLE") {
        return "Unskippable Text";
    }
    if (name == "SFX") {
        return std::format("Sound {}", findArgument(code, 0));
    }
    if (name == "OCARINA") {
        return "Ocarina Playing";
    }
    if (name == "BACKGROUND") {
        return "Background Picture";
    }
    return makeWordsOf(code);
}

} // namespace toki::message
