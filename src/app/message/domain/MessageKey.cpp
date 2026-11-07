#include "MessageKey.h"

#include <format>
#include <optional>
#include <string>

namespace toki::message {

MessageKey makeMessageKey(const MessageTable& table, const usize index) {
    const Message& message = table.messages[index];
    MessageKey key{.id = message.id, .macro = message.macro};
    for (usize before = 0; before < index; ++before) {
        const Message& other = table.messages[before];
        if (other.id == message.id && other.macro == message.macro) {
            ++key.occurrence;
        }
    }
    return key;
}

std::optional<usize> findMessage(const MessageTable& table, const MessageKey& key) {
    usize occurrence = 0;
    for (usize index = 0; index < table.messages.size(); ++index) {
        const Message& message = table.messages[index];
        if (message.id != key.id || message.macro != key.macro) {
            continue;
        }
        if (occurrence == key.occurrence) {
            return index;
        }
        ++occurrence;
    }
    return std::nullopt;
}

std::string formatMessageKey(const MessageKey& key) {
    return std::format("message:{:04X}:{}:{}", key.id, static_cast<u32>(key.macro), key.occurrence);
}

} // namespace toki::message
