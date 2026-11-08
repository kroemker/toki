#pragma once

#include "app/message/Types.h"
#include "app/message/domain/MessageDrawing.h"
#include "app/message/domain/MessageLayout.h"
#include "app/message/domain/MessageLoader.h"

#include <compare>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace toki::message {

/// A box's frame run on the worker, for the content `key` names.
struct FinishedFrame {
    u64 key = 0;
    BoxFrame frame;
};

/// Where a frame is asked for: one of the slots of one owner, as a tab's
/// boxes are each a slot of the tab.
struct BoxSlot {
    u64 owner = 0;
    usize index = 0;

    friend auto operator<=>(const BoxSlot&, const BoxSlot&) = default;
};

/// Runs the frames of message boxes, their display lists encoded through the
/// checkout's gbi and run on the RSP and RDP, on a thread of its own, so a
/// box shows a frame or two late rather than holding the UI while its gbi
/// commands are expanded. One worker serves every tab, so a command expanded
/// for one is not expanded again for the next. Each request is for a slot,
/// and one asked for a slot replaces one not yet started; what it was asked
/// for keeps the catalog alive. Every member function is called from the UI
/// thread.
class BoxFrameWorker {
public:
    BoxFrameWorker();
    BoxFrameWorker(const BoxFrameWorker&) = delete;
    BoxFrameWorker& operator=(const BoxFrameWorker&) = delete;
    BoxFrameWorker(BoxFrameWorker&&) = delete;
    BoxFrameWorker& operator=(BoxFrameWorker&&) = delete;
    ~BoxFrameWorker();

    /// A number no other owner of slots has.
    [[nodiscard]] u64 makeOwner();

    /// Drops what `owner` asked for and what was finished for it.
    void forget(u64 owner);

    /// Asks for the frame of `box`, drawn with the art of `catalog`, for slot
    /// `slot`. Asking again for the content `key` names does nothing.
    void request(BoxSlot slot, u64 key, LaidBox box, SharedCatalog catalog);

    /// The frame last finished for `slot`, once, if one finished since.
    [[nodiscard]] std::optional<FinishedFrame> takeFinished(BoxSlot slot);

private:
    struct Job {
        u64 key = 0;
        LaidBox box;
        SharedCatalog catalog;
    };

    void run(const std::stop_token& stop);

    std::mutex mutex;
    std::condition_variable_any woken;
    std::map<BoxSlot, Job> waiting;
    std::map<BoxSlot, u64> asked;
    std::map<BoxSlot, FinishedFrame> finished;
    u64 nextOwner = 1;
    GbiCommandCache gbiCommandCache;
    /// The catalog whose gbi the cache holds commands of, kept alive so that
    /// no gbi read later is taken for it at the same address.
    SharedCatalog cachedCatalog;
    std::jthread thread;
};

} // namespace toki::message
