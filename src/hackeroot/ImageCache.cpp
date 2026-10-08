#include "ImageCache.h"

#include "hackeroot/AssetImage.h"
#include "hackeroot/FilePoller.h"
#include "z64/MemoryImage.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace hackeroot {

std::shared_ptr<const z64::MemoryImage>
ImageCache::findOrRead(const SharedImageFiles& files,
                       const std::span<const std::shared_ptr<const z64::MemoryImage>> earlier,
                       const ReadImage& read) {
    const std::scoped_lock lock(mutex);
    auto entry = std::ranges::find(entries, files.files, &Entry::files);
    if (entry == entries.end()) {
        entry = entries.insert(entry, Entry{.files = files.files, .poller = FilePoller({})});
    }
    const bool isCurrent = entry->image && std::ranges::equal(entry->earlier, earlier) &&
                           entry->poller.findChanges().empty();
    if (!isCurrent) {
        entry->poller = FilePoller(files.watched);
        entry->image = std::make_shared<const z64::MemoryImage>(read());
        entry->earlier.assign(earlier.begin(), earlier.end());
    }
    return entry->image;
}

} // namespace hackeroot
