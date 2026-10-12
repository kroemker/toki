#pragma once

#include "hackeroot/Types.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hackeroot {

/// A setting of the checkout's configuration headers, `include/config/*.h`:
/// the `#define` the build reads for it, in the branch its conditionals take,
/// what it is written as there, the comment that documents it, and what the
/// build ends with once `config_safeguards.h` has had its say. A setting the
/// safeguards undefine has no effective value.
struct ConfigDefine {
    std::string name;
    /// Under the checkout, `include/config/config_debug.h`.
    std::filesystem::path file;
    usize line = 0;
    std::string written;
    std::string comment;
    std::optional<std::string> effective;
};

/// The settings of the configuration headers the build reads under
/// `defineArguments`, the build's `-D` flags, file by file in the order the
/// build includes them and line by line, each where the build defines it
/// last. Function-like macros and `config_safeguards.h`, which overrides
/// settings rather than holding them, are left out.
[[nodiscard]] std::vector<ConfigDefine>
readConfigDefines(const std::filesystem::path& repository,
                  std::span<const std::string> defineArguments);

} // namespace hackeroot
