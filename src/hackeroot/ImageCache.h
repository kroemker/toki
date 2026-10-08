#pragma once

#include "hackeroot/AssetImage.h"
#include "hackeroot/FilePoller.h"
#include "z64/MemoryImage.h"

#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace hackeroot {

/// The shared images of one workspace: the game code, `gameplay_keep` and
/// every object, read once for every reader of the workspace, whichever loader
/// and thread asks first. An image is read again only when one of the files it
/// watches changed, or when an image it was laid out after was read again.
///
/// One image is read at a time: a reader asking for an image another is
/// reading waits for it rather than reading it again.
class ImageCache {
public:
    using ReadImage = std::function<z64::MemoryImage()>;

    ImageCache() = default;
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;
    ImageCache(ImageCache&&) = delete;
    ImageCache& operator=(ImageCache&&) = delete;
    ~ImageCache() = default;

    /// The image of `files` laid out after `earlier`, made with `read` when
    /// there is none to give yet.
    [[nodiscard]] std::shared_ptr<const z64::MemoryImage>
    findOrRead(const SharedImageFiles& files,
               std::span<const std::shared_ptr<const z64::MemoryImage>> earlier,
               const ReadImage& read);

private:
    struct Entry {
        std::vector<SegmentFiles> files;
        std::vector<std::shared_ptr<const z64::MemoryImage>> earlier;
        FilePoller poller;
        std::shared_ptr<const z64::MemoryImage> image;
    };

    std::mutex mutex;
    std::vector<Entry> entries;
};

} // namespace hackeroot
