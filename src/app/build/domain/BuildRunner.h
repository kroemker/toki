#pragma once

#include "app/build/Types.h"
#include "app/build/domain/BuildLog.h"
#include "app/build/domain/Process.h"
#include "base/Diagnostic.h"
#include "hackeroot/BuildCommand.h"
#include "makeparser/Makefile.h"

#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace toki::build {

enum class BuildStatus : u8 {
    Idle,
    Running,
    Succeeded,
    Failed,
    Stopped,
};

/// How a build that just ended went, for the app's log.
struct BuildOutcome {
    BuildStatus status = BuildStatus::Idle;
    std::string summary;
};

/// Runs make for the checkout, one build at a time, and streams its output
/// into a `BuildLog` as it arrives. The process is read on a worker thread;
/// every member function is called from the UI thread.
class BuildRunner {
public:
    BuildRunner() = default;
    BuildRunner(const BuildRunner&) = delete;
    BuildRunner& operator=(const BuildRunner&) = delete;
    BuildRunner(BuildRunner&&) = delete;
    BuildRunner& operator=(BuildRunner&&) = delete;

    /// Stops a running build and waits for its worker.
    ~BuildRunner();

    /// Builds the checkout at `root`, or builds and runs it, with `settings` as
    /// make's command line.
    [[nodiscard]] std::expected<void, base::Diagnostic>
    start(const std::filesystem::path& root, const makeparser::MakeOverrides& settings,
          hackeroot::BuildTarget target);

    /// Runs `arguments` as a build. What `start` does once it has the command.
    [[nodiscard]] std::expected<void, base::Diagnostic>
    startCommand(const std::vector<std::string>& arguments);

    /// Ends the running build and everything it started.
    void stop();

    /// Moves the output that has arrived into the log. Called once per frame;
    /// returns the outcome when the build ended since the last call.
    [[nodiscard]] std::optional<BuildOutcome> update();

    [[nodiscard]] bool isRunning() const {
        return status == BuildStatus::Running;
    }

    [[nodiscard]] BuildStatus getStatus() const {
        return status;
    }

    [[nodiscard]] const BuildLog& getLog() const {
        return log;
    }

    [[nodiscard]] BuildLog& getLog() {
        return log;
    }

private:
    void runWorker(Process& running);
    void publish(std::vector<std::string> lines);
    [[nodiscard]] BuildOutcome finishBuild(std::optional<s32> exitCode);

    BuildLog log;
    BuildStatus status = BuildStatus::Idle;
    bool stopRequested = false;
    std::chrono::steady_clock::time_point startedAt;
    std::unique_ptr<Process> process;

    std::mutex mutex;
    std::vector<std::string> pending;
    bool finished = false;
    std::optional<s32> exitCode;

    std::jthread worker;
};

} // namespace toki::build
