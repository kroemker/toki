#pragma once

#include "app/assets/Types.h"
#include "hackeroot/AssetImage.h"
#include "hackeroot/Assets.h"
#include "hackeroot/FilePoller.h"
#include "hackeroot/Workspace.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace toki::assets {

/// Reads what is open on a worker thread with `read`, and reads it again
/// whenever one of the files `listWatched` names for it changes, or one of
/// those `listReadWatched` names for what was read, so an export shows without
/// a restart. What is read is an `Input`, which has a `name`: an asset folder,
/// or the layers of a scene whose actors are read. The files are polled,
/// because Windows sends no change notifications for a checkout on
/// `\\wsl.localhost`. Opening something else replaces what is watched. The system headers the reads
/// need are kept between reads of one workspace.
///
/// Every member function is called from the UI thread.
template <typename Input, typename Result>
class WatchedReader {
public:
    using Read = std::function<Result(hackeroot::AssetImageReader&, const Input&)>;
    using ListWatched = std::function<std::vector<std::filesystem::path>(
        const hackeroot::Workspace&, const Input&)>;
    using ListReadWatched = std::function<std::vector<std::filesystem::path>(const Result&)>;

    static constexpr std::chrono::milliseconds PollInterval{500};

    WatchedReader(Read read, ListWatched listWatched, ListReadWatched listReadWatched = {})
        : read(std::move(read)), listWatched(std::move(listWatched)),
          listReadWatched(std::move(listReadWatched)),
          worker([this](const std::stop_token& stop) { runWorker(stop); }) {}

    WatchedReader(const WatchedReader&) = delete;
    WatchedReader& operator=(const WatchedReader&) = delete;
    WatchedReader(WatchedReader&&) = delete;
    WatchedReader& operator=(WatchedReader&&) = delete;
    ~WatchedReader() = default;

    /// Reads `input` against `workspace`, which is kept alive for as long as
    /// it is watched.
    void open(const std::shared_ptr<const hackeroot::Workspace>& workspace, const Input& input) {
        {
            const std::scoped_lock lock(mutex);
            ++generation;
            pending = Request{.workspace = workspace, .input = input, .generation = generation};
            finished.reset();
        }
        wake.notify_all();
    }

    /// Stops reading and watching what is open. A read already under way
    /// finishes, and is dropped.
    void close() {
        const std::scoped_lock lock(mutex);
        ++generation;
        pending.reset();
        finished.reset();
    }

    /// What the worker has read since the last call, first or again.
    [[nodiscard]] std::optional<Result> update() {
        const std::scoped_lock lock(mutex);
        return std::exchange(finished, std::nullopt);
    }

    /// The name of what is being read, if anything.
    [[nodiscard]] std::optional<std::string> findReadingName() const {
        const std::scoped_lock lock(mutex);
        if (pending) {
            return pending->input.name;
        }
        if (readingGeneration != generation) {
            return std::nullopt;
        }
        return reading;
    }

private:
    struct Request {
        std::shared_ptr<const hackeroot::Workspace> workspace;
        Input input;
        u64 generation = 0;
    };

    [[nodiscard]] bool isCurrent(const Request& request) const {
        const std::scoped_lock lock(mutex);
        return request.generation == generation;
    }

    [[nodiscard]] std::optional<Request> waitForRequest(const std::stop_token& stop) {
        std::unique_lock lock(mutex);
        const bool woken =
            wake.wait_for(lock, stop, PollInterval, [this] { return pending.has_value(); });
        if (!woken || stop.stop_requested()) {
            return std::nullopt;
        }
        return std::exchange(pending, std::nullopt);
    }

    void runWorker(const std::stop_token& stop) {
        std::optional<Request> watching;
        std::optional<hackeroot::FilePoller> poller;
        std::optional<hackeroot::FilePoller> readPoller;
        std::optional<hackeroot::AssetImageReader> reader;
        while (!stop.stop_requested()) {
            std::optional<Request> request = waitForRequest(stop);
            if (stop.stop_requested()) {
                return;
            }
            if (!request) {
                if (watching && !isCurrent(*watching)) {
                    watching.reset();
                    poller.reset();
                    readPoller.reset();
                }
                const bool changed = watching && poller &&
                                     (!poller->findChanges().empty() ||
                                      (readPoller && !readPoller->findChanges().empty()));
                if (!changed) {
                    continue;
                }
                request = watching;
            }
            {
                const std::scoped_lock lock(mutex);
                reading = request->input.name;
                readingGeneration = request->generation;
            }
            std::vector<std::filesystem::path> watched =
                listWatched(*request->workspace, request->input);
            poller.emplace(std::move(watched));
            if (!reader || &reader->getWorkspace() != request->workspace.get()) {
                reader.emplace(request->workspace);
            }
            Result result = read(*reader, request->input);
            readPoller.reset();
            if (listReadWatched) {
                readPoller.emplace(listReadWatched(result));
            }
            watching = std::move(request);

            const std::scoped_lock lock(mutex);
            if (watching->generation == generation) {
                finished = std::move(result);
            }
            reading.reset();
        }
    }

    Read read;
    ListWatched listWatched;
    ListReadWatched listReadWatched;
    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<Request> pending;
    std::optional<std::string> reading;
    u64 readingGeneration = 0;
    std::optional<Result> finished;
    u64 generation = 0;

    std::jthread worker;
};

/// A `WatchedReader` of the asset folder open in the asset browser.
template <typename Result>
using WatchedAssetReader = WatchedReader<hackeroot::AssetDirectory, Result>;

} // namespace toki::assets
