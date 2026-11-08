#include "BoxFrameWorker.h"

#include "app/message/domain/MessageCatalog.h"

#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace toki::message {

namespace {

template <typename Value>
void eraseOwner(std::map<BoxSlot, Value>& slots, const u64 owner) {
    slots.erase(slots.lower_bound(BoxSlot{.owner = owner, .index = 0}),
                slots.lower_bound(BoxSlot{.owner = owner + 1, .index = 0}));
}

} // namespace

BoxFrameWorker::BoxFrameWorker() : thread([this](const std::stop_token& stop) { run(stop); }) {}

BoxFrameWorker::~BoxFrameWorker() {
    thread.request_stop();
    woken.notify_all();
}

u64 BoxFrameWorker::makeOwner() {
    const std::scoped_lock lock(mutex);
    return nextOwner++;
}

void BoxFrameWorker::forget(const u64 owner) {
    const std::scoped_lock lock(mutex);
    eraseOwner(waiting, owner);
    eraseOwner(asked, owner);
    eraseOwner(finished, owner);
}

void BoxFrameWorker::request(const BoxSlot slot, const u64 key, LaidBox box,
                             SharedCatalog catalog) {
    {
        const std::scoped_lock lock(mutex);
        const auto found = asked.find(slot);
        if (found != asked.end() && found->second == key) {
            return;
        }
        asked[slot] = key;
        waiting[slot] = Job{.key = key, .box = std::move(box), .catalog = std::move(catalog)};
    }
    woken.notify_one();
}

std::optional<FinishedFrame> BoxFrameWorker::takeFinished(const BoxSlot slot) {
    const std::scoped_lock lock(mutex);
    const auto found = finished.find(slot);
    if (found == finished.end()) {
        return std::nullopt;
    }
    FinishedFrame frame = std::move(found->second);
    finished.erase(found);
    return frame;
}

void BoxFrameWorker::run(const std::stop_token& stop) {
    while (true) {
        BoxSlot slot;
        Job job;
        {
            std::unique_lock lock(mutex);
            if (!woken.wait(lock, stop, [&] { return !waiting.empty(); })) {
                return;
            }
            const auto first = waiting.begin();
            slot = first->first;
            job = std::move(first->second);
            waiting.erase(first);
        }
        cachedCatalog = job.catalog;
        BoxFrame frame = runBoxFrame(job.box, job.catalog->art, gbiCommandCache);
        const std::scoped_lock lock(mutex);
        const auto found = asked.find(slot);
        if (found != asked.end() && found->second == job.key) {
            finished[slot] = FinishedFrame{.key = job.key, .frame = std::move(frame)};
        }
    }
}

} // namespace toki::message
