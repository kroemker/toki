#pragma once

#include "hackeroot/FilePoller.h"
#include "hackeroot/Workspace.h"
#include "makeparser/Makefile.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace toki::workspace {

/// Keeps the workspace of the configured checkout current. Reading a checkout
/// takes seconds over `\\wsl.localhost`, so it happens on a worker thread and
/// the UI keeps the previous workspace until the new one is ready. Between
/// reads the worker polls the files the workspace was read from, and reads it
/// again when one of them changes: editing `config_debug.h` is enough for the
/// tables to follow.
///
/// Every member function is called from the UI thread.
class WorkspaceService {
public:
    explicit WorkspaceService(std::chrono::milliseconds pollInterval = std::chrono::seconds(1));

    WorkspaceService(const WorkspaceService&) = delete;
    WorkspaceService(WorkspaceService&&) = delete;
    WorkspaceService& operator=(const WorkspaceService&) = delete;
    WorkspaceService& operator=(WorkspaceService&&) = delete;
    ~WorkspaceService() = default;

    /// Reads the checkout at `root` with `buildSettings` as make's command line,
    /// replacing the current workspace once the read finishes.
    void open(std::filesystem::path root, makeparser::MakeOverrides buildSettings);

    /// Takes over a read the worker has finished. Called once per frame; true
    /// when the workspace changed, so views built from the old one can rebuild.
    bool update();

    /// Nothing until the first read has finished.
    [[nodiscard]] const hackeroot::Workspace* getWorkspace() const {
        return workspace.get();
    }

    /// The current workspace for work that outlives a frame, such as reading
    /// an asset on another thread; it stays valid after a newer one replaces it.
    [[nodiscard]] std::shared_ptr<const hackeroot::Workspace> getSharedWorkspace() const {
        return workspace;
    }

    [[nodiscard]] bool isLoading() const;

private:
    struct Request {
        std::filesystem::path root;
        makeparser::MakeOverrides buildSettings;
    };

    void runWorker(const std::stop_token& stop);

    [[nodiscard]] std::optional<hackeroot::FilePoller> readCheckout(const Request& request);

    std::chrono::milliseconds pollInterval;
    // Shared, not owned: a workspace is immutable once read, and background
    // reads of its assets must keep it alive when the checkout is read again
    // and the UI moves on to the new one.
    std::shared_ptr<const hackeroot::Workspace> workspace;

    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<Request> pending;
    std::unique_ptr<hackeroot::Workspace> finished;
    bool loading = false;

    std::jthread worker;
};

} // namespace toki::workspace
