#include "WorkspaceService.h"

#include "app/core/domain/Log.h"
#include "app/core/domain/Utf8.h"
#include "base/Diagnostic.h"
#include "hackeroot/FilePoller.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"
#include "makeparser/Makefile.h"

#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace toki::workspace {

namespace {

void reportDiagnostics(const std::vector<base::Diagnostic>& diagnostics) {
    for (const base::Diagnostic& diagnostic : diagnostics) {
        switch (diagnostic.severity) {
        case base::Severity::Note:
            core::logDebug("{}", base::describe(diagnostic));
            break;
        case base::Severity::Warning:
            core::logWarn("{}", base::describe(diagnostic));
            break;
        case base::Severity::Error:
            core::logError("{}", base::describe(diagnostic));
            break;
        }
    }
}

} // namespace

WorkspaceService::WorkspaceService(std::chrono::milliseconds pollInterval)
    : pollInterval(pollInterval), worker([this](const std::stop_token& stop) { runWorker(stop); }) {
}

void WorkspaceService::open(std::filesystem::path root, makeparser::MakeOverrides buildSettings) {
    {
        const std::scoped_lock lock(mutex);
        pending = Request{.root = std::move(root), .buildSettings = std::move(buildSettings)};
    }
    wake.notify_all();
}

bool WorkspaceService::update() {
    const std::scoped_lock lock(mutex);
    if (!finished) {
        return false;
    }
    workspace = std::move(finished);
    return true;
}

bool WorkspaceService::isLoading() const {
    const std::scoped_lock lock(mutex);
    return loading || pending.has_value();
}

void WorkspaceService::runWorker(const std::stop_token& stop) {
    std::optional<Request> active;
    std::optional<hackeroot::FilePoller> poller;

    while (!stop.stop_requested()) {
        std::optional<Request> requested;
        {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, stop, pollInterval, [this] { return pending.has_value(); });
            if (stop.stop_requested()) {
                return;
            }
            requested = std::exchange(pending, std::nullopt);
            loading = requested.has_value();
        }

        if (requested) {
            active = std::move(requested);
        } else if (poller && active) {
            const std::vector<std::filesystem::path> changed = poller->findChanges();
            if (changed.empty()) {
                continue;
            }
            core::logInfo("{} changed; reading the checkout again",
                          core::encodeUtf8(changed.front()));
        } else {
            continue;
        }

        if (std::optional<hackeroot::FilePoller> next = readCheckout(*active)) {
            poller = std::move(next);
        } else if (!poller) {
            poller.emplace(std::vector<std::filesystem::path>{active->root / "Makefile"});
        }
    }
}

std::optional<hackeroot::FilePoller> WorkspaceService::readCheckout(const Request& request) {
    {
        const std::scoped_lock lock(mutex);
        loading = true;
    }
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::expected<hackeroot::Workspace, base::Diagnostic> read =
        hackeroot::Workspace::load(request.root, request.buildSettings);
    if (!read) {
        core::logError("{}", base::describe(read.error()));
        const std::scoped_lock lock(mutex);
        loading = false;
        return std::nullopt;
    }

    reportDiagnostics(read->collectDiagnostics());
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    const hackeroot::Tables& tables = read->getTables();
    core::logOk("read {} for {} in {} ms: {} scenes, {} actors, {} objects, {} entrances",
                core::encodeUtf8(request.root), read->getBuildDefines().version, elapsed.count(),
                tables.scenes.size(), tables.actors.size(), tables.objects.size(),
                tables.entrances.size());

    hackeroot::FilePoller poller(read->listWatchedFiles());
    const std::scoped_lock lock(mutex);
    finished = std::make_unique<hackeroot::Workspace>(std::move(*read));
    loading = false;
    return poller;
}

} // namespace toki::workspace
