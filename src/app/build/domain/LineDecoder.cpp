#include "LineDecoder.h"

#include "app/build/Types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::build {

namespace {

constexpr std::string_view Replacement = "\xEF\xBF\xBD";

usize findSequenceLength(const std::string_view bytes, const usize start) {
    const auto lead = static_cast<u8>(bytes[start]);
    usize length = 0;
    u32 minimum = 0;
    u32 codePoint = 0;
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        minimum = 0x80;
        codePoint = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        minimum = 0x800;
        codePoint = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        minimum = 0x10000;
        codePoint = lead & 0x07;
    } else {
        return 0;
    }
    if (start + length > bytes.size()) {
        return 0;
    }
    for (usize i = 1; i < length; ++i) {
        const auto next = static_cast<u8>(bytes[start + i]);
        if ((next & 0xC0) != 0x80) {
            return 0;
        }
        codePoint = (codePoint << 6) | (next & 0x3F);
    }
    const bool surrogate = codePoint >= 0xD800 && codePoint <= 0xDFFF;
    if (codePoint < minimum || codePoint > 0x10FFFF || surrogate) {
        return 0;
    }
    return length;
}

std::string finishLine(std::string_view line) {
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    if (const usize redraw = line.rfind('\r'); redraw != std::string_view::npos) {
        line.remove_prefix(redraw + 1);
    }
    return makeValidUtf8(line);
}

} // namespace

std::string makeValidUtf8(const std::string_view bytes) {
    std::string valid;
    valid.reserve(bytes.size());
    usize position = 0;
    while (position < bytes.size()) {
        const usize length = findSequenceLength(bytes, position);
        if (length == 0) {
            valid += Replacement;
            ++position;
            continue;
        }
        valid += bytes.substr(position, length);
        position += length;
    }
    return valid;
}

std::vector<std::string> LineDecoder::decode(const std::string_view bytes) {
    partial += bytes;
    std::vector<std::string> lines;
    usize start = 0;
    for (usize end = partial.find('\n'); end != std::string::npos;
         end = partial.find('\n', start)) {
        lines.push_back(finishLine(std::string_view(partial).substr(start, end - start)));
        start = end + 1;
    }
    partial.erase(0, start);
    return lines;
}

std::optional<std::string> LineDecoder::finish() {
    if (partial.empty()) {
        return std::nullopt;
    }
    std::string line = finishLine(partial);
    partial.clear();
    return line;
}

} // namespace toki::build
