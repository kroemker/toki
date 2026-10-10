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
    return DatabaseSnapshot{.database = readActorDatabase(files.checkout),
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
    {
        const std::scoped_lock lock(mutex);
        pending.push_back(std::move(edit));
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

std::optional<std::string> DatabaseStore::seed(const Opened& opened) {
    std::error_code failure;
    if (opened.workspace == nullptr || std::filesystem::exists(opened.files.checkout, failure)) {
        return std::nullopt;
    }
    const ActorDatabase builtIn = readActorDatabase(opened.files.builtIn);
    const ActorDatabase seeded =
        makeSeededDatabase(*opened.workspace, builtIn, [this](const usize done, const usize total) {
            const std::scoped_lock lock(mutex);
            seeding = SeedingProgress{.done = done, .total = total};
        });
    {
        const std::scoped_lock lock(mutex);
        seeding.reset();
    }
    if (const std::expected<void, std::string> saved =
            saveActorDatabase(seeded, opened.files.checkout);
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
        std::deque<Edit> edits;
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
            if (std::optional<std::string> seedFailure = seed(*reopened)) {
                failed.push_back(std::move(*seedFailure));
            }
            files = reopened->files;
            poller.emplace(std::vector{files->builtIn, files->checkout});
        }
        for (const Edit& edit : edits) {
            if (const std::expected<void, std::string> edited = applyEdit(files->checkout, edit);
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
    std::error_code failure;
    std::filesystem::create_directories(file.parent_path(), failure);
    if (const std::expected<void, std::string> saved = saveActorDatabase(database, file); !saved) {
        return std::unexpected(saved.error());
    }
    return {};
}

} // namespace toki::actor
