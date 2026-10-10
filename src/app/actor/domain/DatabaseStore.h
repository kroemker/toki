#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "hackeroot/Workspace.h"

#include <condition_variable>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace toki::actor {

/// Toki's own actor database, which a checkout's is seeded from and entries are
/// derived from, and the checkout's.
struct DatabaseFiles {
    std::filesystem::path builtIn;
    std::filesystem::path checkout;

    friend bool operator==(const DatabaseFiles&, const DatabaseFiles&) = default;
};

/// The checkout's actor database as it was last read, Toki's own, and the
/// files they were read from.
struct DatabaseSnapshot {
    DatabaseFiles files;
    ActorDatabase database;
    ActorDatabase builtIn;
};

/// How far seeding a checkout's database has come: actors derived of all
/// those to derive.
struct SeedingProgress {
    usize done = 0;
    usize total = 0;
};

/// Keeps a checkout's actor database read, and edits it, on a worker thread,
/// since a checkout's file is over `\\wsl.localhost`. A checkout without the
/// file is seeded first: Toki's own database, with every actor of the
/// checkout's actor table it does not cover derived from its overlay. The file
/// is read again whenever it changes, polled as the workspace polls its files,
/// and after every edit. Each edit reads the file as it is now, changes it and
/// saves it; a file that does not read cleanly is not written, since writing
/// it would drop what could not be read. Edits run in the order they were
/// asked for, each on the file it was asked for, even once another checkout is
/// open. Seeding needs Toki's own database to read cleanly, and is not done
/// when whether the file exists cannot be told.
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

    /// Reads `files` of `workspace`'s checkout from now on, unless they are the
    /// files already open.
    void open(DatabaseFiles files, std::shared_ptr<const hackeroot::Workspace> workspace);

    [[nodiscard]] const std::optional<DatabaseFiles>& getFiles() const {
        return openFiles;
    }

    /// Edits the open checkout's database; without one open, it fails.
    void edit(Edit edit);

    /// Edits the database in `file`, which need not be the open one's.
    void editFile(std::filesystem::path file, Edit edit);

    /// The database, read since the last call, if it was.
    [[nodiscard]] std::optional<DatabaseSnapshot> update();

    /// What failed since the last call, each naming the file.
    [[nodiscard]] std::vector<std::string> takeFailures();

    /// Whether an edit or a read is waiting or running.
    [[nodiscard]] bool isBusy() const;

    /// How far seeding has come, while a checkout's database is being seeded.
    [[nodiscard]] std::optional<SeedingProgress> findSeeding() const;

private:
    struct Opened {
        DatabaseFiles files;
        /// Shared with the workspace service, which replaces its workspace on
        /// a reload while seeding may still be reading this one.
        std::shared_ptr<const hackeroot::Workspace> workspace;
    };

    struct PendingEdit {
        std::filesystem::path file;
        Edit edit;
    };

    void runWorker(const std::stop_token& stop);
    [[nodiscard]] std::optional<std::string> seed(const Opened& opened,
                                                  const std::stop_token& stop);

    std::optional<DatabaseFiles> openFiles;

    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<Opened> requested;
    std::deque<PendingEdit> pending;
    bool isWorking = false;
    std::optional<SeedingProgress> seeding;
    std::optional<DatabaseSnapshot> finished;
    std::vector<std::string> failures;
    std::jthread worker;
};

/// Applies `edit` to `file` as the store's worker does, on the calling thread.
[[nodiscard]] std::expected<void, std::string> applyEdit(const std::filesystem::path& file,
                                                         const DatabaseStore::Edit& edit);

} // namespace toki::actor
