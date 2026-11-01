#include "Message.h"

namespace toki::message {

const char* getLanguageName(const Language language) {
    switch (language) {
    case Language::Japanese:
        return "Japanese";
    case Language::English:
        return "English";
    case Language::German:
        return "German";
    case Language::French:
        return "French";
    }
    return "";
}

bool isInLanguage(const Message& message, const Language language,
                  const bool isFontMessageEnglish) {
    const bool isJapanese = language == Language::Japanese;
    switch (message.macro) {
    case MessageMacro::Message:
        return true;
    case MessageMacro::JapaneseOnly:
        return isJapanese;
    case MessageMacro::NesOnly:
        return !isJapanese;
    case MessageMacro::FontMessage:
        return isJapanese != isFontMessageEnglish;
    }
    return true;
}

} // namespace toki::message
