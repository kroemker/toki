#pragma once

#include "hackeroot/Types.h"
#include "z64/MemoryImage.h"

#include <filesystem>
#include <span>
#include <vector>

namespace hackeroot {

class Workspace;

/// The C files that make one segment, in the order the build links them: a
/// scene's files for segment 2, a room's for segment 3, an object's for
/// segment 6.
struct SegmentFiles {
    u8 segment = 0;
    std::vector<std::filesystem::path> files;
};

/// Reads asset C into a memory image, as the build would compile and link it.
/// Each file is read against the headers it includes, under the build's
/// configuration, so its types have their real layouts and its macros their
/// real definitions. Every defined object is encoded where it was first
/// declared other than `extern`, as GCC places a tentative definition,
/// textures and palettes from the files the build generates them from, and
/// symbols resolve across all the segments given, so a room's display lists
/// may use its scene's palettes. The linker's symbols for where files sit in
/// the ROM (`_spot00_room_0SegmentRomStart`) read as zero: Toki64 reads no ROM.
/// The game code's data that assets point at, such as `gIdentityMtx`, is in
/// segment 0 (`z64::getCodeSymbols`).
/// A file that cannot be read, a type no header defines, and a symbol nothing
/// defines are diagnostics in the image.
[[nodiscard]] z64::MemoryImage readAssetImage(const Workspace& workspace,
                                              std::span<const SegmentFiles> segments);

} // namespace hackeroot
