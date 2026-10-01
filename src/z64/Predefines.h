#pragma once

#include <span>
#include <string>

namespace z64 {

/// Turns the arguments a build passes to `cpp` into the source text that means
/// the same thing, the way a compiler does for its command line: `-DNAME`
/// becomes `#define NAME 1`, `-DNAME=VALUE` becomes `#define NAME VALUE`,
/// `-UNAME` becomes `#undef NAME`, and `-std=` defines `__STDC_VERSION__`.
/// Every other argument is ignored. Arguments are expected with the shell's
/// quoting already removed.
[[nodiscard]] std::string predefineSource(std::span<const std::string> arguments);

} // namespace z64
