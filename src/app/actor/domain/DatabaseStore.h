#pragma once

#include "app/actor/domain/ActorDatabase.h"

#include <condition_variable>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace toki::actor {

/// The shipped actor database and the checkout's.
struct DatabaseFiles {
    std::filesystem::path shipped;
    std::filesystem::path checkout;

    friend bool operator==(const DatabaseFiles&, const DatabaseFiles&) = default;
};

/// Both actor databases as they were last read.
struct DatabaseSnapshot {
    ActorDatabase shipped;
    ActorDatabase checkout;
};

/// Keeps the shipped actor database and a checkout's read, and edits the
/// checkout's, on a worker thread, since a checkout's file is over
/// `\\wsl.localhost`. Both files are read again whenever one changes, polled
/// as the workspace polls its files, and after every edit. Each edit reads the
/// checkout's file as it is now, changes it and saves it; a file that does not
/// read cleanly is not written, since writing it would drop what could not be
/// read. Edits run in the order they were asked for.
///
/// Every member function is called from the UI thread.
class DatabaseStore {
public:
    /// Changes a database, or says why it did not.
    using Edit = std::function<std::expected<void, std::string>(ActorDatabase&)>;

    DatabaseStore();
    DatabaseStore(const DatabaseStore&) = delete;
    DatabaseStore& operator=(const DatabaseStore&) = delete;
    DatabaseStore(DatabaseStore&&) = delete;
    DatabaseStore& operator=(DatabaseStore&&) = delete;
    ~DatabaseStore() = default;

    /// Reads `files` from now on, unless they are the files already open.
    void open(DatabaseFiles files);

    [[nodiscard]] const std::optional<DatabaseFiles>& getFiles() const {
        return openFiles;
    }

    /// Edits the open checkout's database; without one open, it fails.
    void edit(Edit edit);

    /// Both databases, read since the last call, if they were.
    [[nodiscard]] std::optional<DatabaseSnapshot> update();

    /// What failed since the last call, each naming the file.
    [[nodiscard]] std::vector<std::string> takeFailures();

    /// Whether an edit or a read is waiting or running.
    [[nodiscard]] bool isBusy() const;

private:
    void runWorker(const std::stop_token& stop);

    std::optional<DatabaseFiles> openFiles;

    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<DatabaseFiles> requestedFiles;
    std::deque<Edit> pending;
    bool isWorking = false;
    std::optional<DatabaseSnapshot> finished;
    std::vector<std::string> failures;
    std::jthread worker;
};

/// Applies `edit` to `file` as the store's worker does, on the calling thread.
[[nodiscard]] std::expected<void, std::string> applyEdit(const std::filesystem::path& file,
                                                         const DatabaseStore::Edit& edit);

} // namespace toki::actor
