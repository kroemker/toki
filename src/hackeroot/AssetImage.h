#pragma once

#include "hackeroot/Types.h"
#include "z64/MemoryImage.h"

#include <filesystem>
#include <memory>
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

/// Reads images as `readAssetImage` does, again and again against one
/// workspace, and several at once, as the game loads a scene and then each of
/// its rooms into the same segment: every image is laid out on its own, and a
/// symbol one image does not define resolves to where an earlier image laid it
/// out, so each room may use its scene. Only the first image holds the game
/// code's data.
///
/// The system headers each file is read against are kept between reads: they
/// are most of the cost of reading a fast64 export, and a re-export does not
/// change them. Everything else, the files, their textures and the folders
/// they list, is read afresh every time.
class AssetImageReader {
public:
    explicit AssetImageReader(std::shared_ptr<const Workspace> workspace);
    AssetImageReader(const AssetImageReader&) = delete;
    AssetImageReader& operator=(const AssetImageReader&) = delete;
    AssetImageReader(AssetImageReader&&) noexcept;
    AssetImageReader& operator=(AssetImageReader&&) noexcept;
    ~AssetImageReader();

    [[nodiscard]] const Workspace& getWorkspace() const {
        return *workspace;
    }

    [[nodiscard]] std::vector<z64::MemoryImage>
    read(std::span<const std::vector<SegmentFiles>> images);

private:
    struct Preludes;

    // Shared with whoever else reads the workspace: it is replaced, not
    // changed, when the checkout is read again.
    std::shared_ptr<const Workspace> workspace;
    std::unique_ptr<Preludes> preludes;
};

} // namespace hackeroot
