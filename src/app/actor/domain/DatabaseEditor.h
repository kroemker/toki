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

/// Edits an actor database file on a worker thread, since a checkout's file is
/// over `\\wsl.localhost`: each edit reads the file as it is now, changes it
/// and saves it. A file that does not read cleanly is not written, since
/// writing it would drop what could not be read. Edits run in the order they
/// were asked for.
///
/// Every member function is called from the UI thread.
class DatabaseEditor {
public:
    /// Changes a database, or says why it did not.
    using Edit = std::function<std::expected<void, std::string>(ActorDatabase&)>;

    DatabaseEditor();
    DatabaseEditor(const DatabaseEditor&) = delete;
    DatabaseEditor& operator=(const DatabaseEditor&) = delete;
    DatabaseEditor(DatabaseEditor&&) = delete;
    DatabaseEditor& operator=(DatabaseEditor&&) = delete;
    ~DatabaseEditor() = default;

    void edit(std::filesystem::path file, Edit edit);

    /// What failed since the last call, each naming the file.
    [[nodiscard]] std::vector<std::string> takeFailures();

    /// Whether an edit is waiting or being written.
    [[nodiscard]] bool isBusy() const;

private:
    struct Request {
        std::filesystem::path file;
        Edit edit;
    };

    void runWorker(const std::stop_token& stop);

    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<Request> pending;
    bool isWriting = false;
    std::vector<std::string> failures;
    std::jthread worker;
};

/// Applies `edit` to `file` as the editor's worker does, on the calling thread.
[[nodiscard]] std::expected<void, std::string> applyEdit(const std::filesystem::path& file,
                                                         const DatabaseEditor::Edit& edit);

} // namespace toki::actor
