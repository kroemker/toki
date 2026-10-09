#include "DatabaseEditor.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "base/Diagnostic.h"

#include <expected>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace toki::actor {

DatabaseEditor::DatabaseEditor()
    : worker([this](const std::stop_token& stop) { runWorker(stop); }) {}

void DatabaseEditor::edit(std::filesystem::path file, Edit edit) {
    {
        const std::scoped_lock lock(mutex);
        pending.push_back(Request{.file = std::move(file), .edit = std::move(edit)});
    }
    wake.notify_one();
}

std::vector<std::string> DatabaseEditor::takeFailures() {
    const std::scoped_lock lock(mutex);
    return std::exchange(failures, {});
}

bool DatabaseEditor::isBusy() const {
    const std::scoped_lock lock(mutex);
    return isWriting || !pending.empty();
}

void DatabaseEditor::runWorker(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        std::optional<Request> request;
        {
            std::unique_lock lock(mutex);
            if (!wake.wait(lock, stop, [this] { return !pending.empty(); })) {
                return;
            }
            request = std::move(pending.front());
            pending.pop_front();
            isWriting = true;
        }
        const std::expected<void, std::string> edited = applyEdit(request->file, request->edit);
        const std::scoped_lock lock(mutex);
        isWriting = false;
        if (!edited) {
            failures.push_back(edited.error());
        }
    }
}

std::expected<void, std::string> applyEdit(const std::filesystem::path& file,
                                           const DatabaseEditor::Edit& edit) {
    ActorDatabase database = readActorDatabase(file);
    if (!database.diagnostics.empty()) {
        return std::unexpected(std::format(
            "{} is not written, since it does not read cleanly and writing it would drop what "
            "could not be read: {}",
            file.string(), base::describe(database.diagnostics.front())));
    }
    if (const std::expected<void, std::string> edited = edit(database); !edited) {
        return std::unexpected(std::format("{}: {}", file.string(), edited.error()));
    }
    std::error_code failure;
    std::filesystem::create_directories(file.parent_path(), failure);
    if (const std::expected<void, std::string> saved = saveActorDatabase(database, file); !saved) {
        return std::unexpected(saved.error());
    }
    return {};
}

} // namespace toki::actor
