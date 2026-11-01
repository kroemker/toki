#pragma once

#include "app/message/domain/Message.h"
#include "cparser/SourceSet.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace cparser {
class MacroTable;
} // namespace cparser

namespace hackeroot {
class Workspace;
} // namespace hackeroot

namespace toki::message {

/// Where the build reads messages from: `assets/text/message_data.h`, which
/// includes the vanilla ones from `extracted/<version>/text/` and adds the
/// checkout's own.
struct MessageFiles {
    std::filesystem::path table;
    std::filesystem::path vanillaDirectory;
    std::vector<std::filesystem::path> searchPaths;
};

[[nodiscard]] MessageFiles findMessageFiles(const std::filesystem::path& repository,
                                            std::string_view version);

/// Reads every `DEFINE_MESSAGE` of `files.table` and what it includes,
/// answering its conditionals against `macros`, the build configuration's
/// layer. A message that cannot be read is a diagnostic naming it and is left
/// out; the rest are read. `pendingFiles` are read in place of what is on
/// disk, so a file about to be written is read back as it will be.
[[nodiscard]] MessageTable readMessageTable(const cparser::MacroTable& macros,
                                            const MessageFiles& files,
                                            const cparser::PendingFiles* pendingFiles = nullptr);

/// The messages of the checkout `workspace` read, for its `VERSION`.
[[nodiscard]] MessageTable readMessages(const hackeroot::Workspace& workspace);

} // namespace toki::message
