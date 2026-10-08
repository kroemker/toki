#pragma once

#include "base/Diagnostic.h"
#include "hackeroot/Types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cparser {
class MacroTable;
} // namespace cparser

namespace hackeroot {

/// One `beginseg` .. `endseg` of the spec: the segment's name and the object
/// files it is linked from, in order, as the spec spells them with
/// `$(BUILD_DIR)/` taken off (`assets/objects/gameplay_keep/gameplay_keep.o`).
struct SpecSegment {
    std::string name;
    std::vector<std::string> includes;
};

/// The ROM's segments as `spec/spec` lists them, its conditionals answered
/// under the build's configuration.
struct Spec {
    std::vector<SpecSegment> segments;
    std::vector<base::Diagnostic> diagnostics;

    [[nodiscard]] const SpecSegment* findSegment(std::string_view name) const;
};

/// Reads `spec/spec` under `repository`, against `macros`, the build
/// configuration's layer. A spec that cannot be read is a diagnostic and no
/// segments.
[[nodiscard]] Spec readSpec(const cparser::MacroTable& macros,
                            const std::filesystem::path& repository);

} // namespace hackeroot
