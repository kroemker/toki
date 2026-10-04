#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::build {

/// Splits a process's output into lines as it arrives, in whatever chunks it
/// arrives. A line ends at `\n`; a `\r` before it is dropped, and a `\r` inside
/// it means the process redrew the line, so only what follows the last one is
/// kept. Every line comes out as valid UTF-8, whatever bytes went in.
class LineDecoder {
public:
    /// The lines `bytes` completes. Bytes after the last newline wait for the
    /// next call.
    [[nodiscard]] std::vector<std::string> decode(std::string_view bytes);

    /// What is left after the output ends, if it did not end with a newline.
    [[nodiscard]] std::optional<std::string> finish();

private:
    std::string partial;
};

/// `bytes` with every byte that does not belong to a well-formed UTF-8
/// sequence replaced by U+FFFD, so output in another encoding shows up marked
/// rather than disappearing.
[[nodiscard]] std::string makeValidUtf8(std::string_view bytes);

} // namespace toki::build
