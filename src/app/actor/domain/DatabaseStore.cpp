#include "DatabaseStore.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/Derivation.h"
#include "base/Diagnostic.h"
#include "hackeroot/FilePoller.h"
#include "hackeroot/Workspace.h"

#include <chrono>
#include <deque>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::chrono::milliseconds PollInterval{500};

[[nodiscard]] DatabaseSnapshot readSnapshot(const DatabaseFiles& files) {
    return DatabaseSnapshot{.files = files,
                            .database = readActorDatabase(files.checkout),
                            .builtIn = readActorDatabase(files.builtIn)};
}

} // namespace

DatabaseStore::DatabaseStore() : worker([this](const std::stop_token& stop) { runWorker(stop); }) {}

void DatabaseStore::open(DatabaseFiles files,
                         std::shared_ptr<const hackeroot::Workspace> workspace) {
    if (openFiles == files) {
        return;
    }
    openFiles = files;
    {
        const std::scoped_lock lock(mutex);
        requested = Opened{.files = std::move(files), .workspace = std::move(workspace)};
        isWorking = true;
    }
    wake.notify_one();
}

void DatabaseStore::edit(Edit edit) {
    if (!openFiles) {
        const std::scoped_lock lock(mutex);
        failures.emplace_back("no checkout is open, so there is no actor database to edit");
        return;
    }
    editFile(openFiles->checkout, std::move(edit));
}

void DatabaseStore::editFile(std::filesystem::path file, Edit edit) {
    {
        const std::scoped_lock lock(mutex);
        pending.push_back(PendingEdit{.file = std::move(file), .edit = std::move(edit)});
    }
    wake.notify_one();
}

std::optional<DatabaseSnapshot> DatabaseStore::update() {
    const std::scoped_lock lock(mutex);
    return std::exchange(finished, std::nullopt);
}

std::vector<std::string> DatabaseStore::takeFailures() {
    const std::scoped_lock lock(mutex);
    return std::exchange(failures, {});
}

bool DatabaseStore::isBusy() const {
    const std::scoped_lock lock(mutex);
    return isWorking || requested.has_value() || !pending.empty();
}

std::optional<SeedingProgress> DatabaseStore::findSeeding() const {
    const std::scoped_lock lock(mutex);
    return seeding;
}

std::optional<std::string> DatabaseStore::seed(const Opened& opened, const std::stop_token& stop) {
    std::error_code failure;
    const bool isSeeded = std::filesystem::exists(opened.files.checkout, failure);
    if (failure) {
        return std::format("{} is not seeded, since whether it exists cannot be told: {}",
                           opened.files.checkout.string(), failure.message());
    }
    if (opened.workspace == nullptr || isSeeded) {
        return std::nullopt;
    }
    const ActorDatabase builtIn = readActorDatabase(opened.files.builtIn);
    if (!builtIn.diagnostics.empty() || builtIn.entries.empty()) {
        return std::format("{} is not seeded, since Toki's own actor database {}",
                           opened.files.checkout.string(),
                           builtIn.diagnostics.empty()
                               ? std::format("at {} has no entries", opened.files.builtIn.string())
                               : std::format("does not read cleanly: {}",
                                             base::describe(builtIn.diagnostics.front())));
    }
    const std::optional<ActorDatabase> seeded = makeSeededDatabase(
        *opened.workspace, builtIn, stop, [this](const usize done, const usize total) {
            const std::scoped_lock lock(mutex);
            seeding = SeedingProgress{.done = done, .total = total};
        });
    {
        const std::scoped_lock lock(mutex);
        seeding.reset();
    }
    if (!seeded) {
        return std::nullopt;
    }
    if (const std::expected<void, std::string> saved =
            saveActorDatabase(*seeded, opened.files.checkout);
        !saved) {
        return saved.error();
    }
    return std::nullopt;
}

void DatabaseStore::runWorker(const std::stop_token& stop) {
    std::optional<DatabaseFiles> files;
    std::optional<hackeroot::FilePoller> poller;
    while (!stop.stop_requested()) {
        std::optional<Opened> reopened;
        std::deque<PendingEdit> edits;
        {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, stop, PollInterval,
                          [this] { return requested.has_value() || !pending.empty(); });
            if (stop.stop_requested()) {
                return;
            }
            reopened = std::exchange(requested, std::nullopt);
            edits = std::exchange(pending, {});
            isWorking = reopened.has_value() || !edits.empty();
        }
        std::vector<std::string> failed;
        if (reopened) {
            if (std::optional<std::string> seedFailure = seed(*reopened, stop)) {
                failed.push_back(std::move(*seedFailure));
            }
            if (stop.stop_requested()) {
                return;
            }
            files = reopened->files;
            poller.emplace(std::vector{files->builtIn, files->checkout});
        }
        for (const PendingEdit& edit : edits) {
            if (const std::expected<void, std::string> edited = applyEdit(edit.file, edit.edit);
                !edited) {
                failed.push_back(edited.error());
            }
        }
        const bool isChanged = poller && !poller->findChanges().empty();
        std::optional<DatabaseSnapshot> read;
        if (files && (reopened || !edits.empty() || isChanged)) {
            read = readSnapshot(*files);
        }
        const std::scoped_lock lock(mutex);
        isWorking = false;
        std::ranges::move(failed, std::back_inserter(failures));
        if (read) {
            finished = std::move(read);
        }
    }
}

std::expected<void, std::string> applyEdit(const std::filesystem::path& file,
                                           const DatabaseStore::Edit& edit) {
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
    if (const std::expected<void, std::string> saved = saveActorDatabase(database, file); !saved) {
        return std::unexpected(saved.error());
    }
    return {};
}

} // namespace toki::actor
