#pragma once

#include "base/Diagnostic.h"
#include "cparser/SourceSet.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Types.h"
#include "z64/MemoryImage.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hackeroot {

/// Whether `name` is a symbol the linker defines for a segment of the spec,
/// such as `_spot04_room_0SegmentRomStart`, which no asset file defines and
/// which an image lays out as zero.
[[nodiscard]] bool isLinkerSymbol(std::string_view name);

class Workspace;

/// The C files that make one segment, in the order the build links them: a
/// scene's files for segment 2, a room's for segment 3, an object's for
/// segment 6.
struct SegmentFiles {
    u8 segment = 0;
    std::vector<std::filesystem::path> files;
    /// Whether the files are an overlay's own C, as `z_en_holl.c` is, rather
    /// than asset C: only the data the RSP draws with is read from them,
    /// display lists, vertices, matrices, lights and integer arrays, and the
    /// animations it poses its skeletons with, which is what an overlay
    /// defines for itself to draw; the rest is its code's.
    /// The overlay's folder is read inline, as its own headers are.
    bool isOverlay = false;

    [[nodiscard]] bool operator==(const SegmentFiles&) const = default;
};

/// An image read once and shared by the reads after it for as long as none of
/// `watched` changes: the game code, or `gameplay_keep` under every object.
struct SharedImageFiles {
    std::vector<SegmentFiles> files;
    std::vector<std::filesystem::path> watched;
};

/// The images one read made: the shared ones first, in the order asked for,
/// then the rest.
struct ImageSet {
    std::vector<std::shared_ptr<const z64::MemoryImage>> shared;
    std::vector<z64::MemoryImage> images;
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
/// defines are diagnostics in the image. A checkout that enables F3DEX3 is not
/// read at all: every image is empty, with a diagnostic saying why.
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
/// change them. Shared images are kept by the workspace (`ImageCache`), for
/// every reader of it, while their files do not change. Everything else, the
/// files, their textures and the folders they list, is read afresh every time.
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

    /// Reads `images` after `shared`, as `read` reads them after the images
    /// before them. A shared image is read again only when one of its watched
    /// files changed or an image before it was read again; otherwise the one an
    /// earlier read made is returned, with its diagnostics. `pendingFiles` are
    /// read in place of the files on disk at their paths, so files can be read
    /// back as they will be written before they are.
    [[nodiscard]] ImageSet read(std::span<const SharedImageFiles> shared,
                                std::span<const std::vector<SegmentFiles>> images,
                                const cparser::PendingFiles* pendingFiles = nullptr);

    /// The system headers `includes` names read as an asset file that includes
    /// them is read against: their types and macros. Kept for as long as the
    /// reader is.
    [[nodiscard]] const Prelude& getPrelude(const std::vector<std::string>& includes);

    /// The value of `expression`, a constant expression of `gbi.h`'s names as
    /// the build reads them (`readGbi`), as a render mode's cycle
    /// `G_RM_ZB_CLD_SURF2` is written. A name the header does not define is a
    /// diagnostic naming it, as is a checkout that enables F3DEX3.
    [[nodiscard]] std::expected<u32, base::Diagnostic> evaluateGbi(std::string_view expression);

private:
    struct Caches;

    // Shared with whoever else reads the workspace: it is replaced, not
    // changed, when the checkout is read again.
    std::shared_ptr<const Workspace> workspace;
    std::unique_ptr<Caches> caches;
};

} // namespace hackeroot
