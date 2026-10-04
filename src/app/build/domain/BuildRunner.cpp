#include "BuildRunner.h"

#include "app/build/Types.h"
#include "app/build/domain/LineDecoder.h"
#include "app/build/domain/Process.h"
#include "base/Diagnostic.h"
#include "hackeroot/BuildCommand.h"
#include "makeparser/Makefile.h"

#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace toki::build {

BuildRunner::~BuildRunner() {
    stop();
}

std::expected<void, base::Diagnostic> BuildRunner::start(const std::filesystem::path& root,
                                                         const makeparser::MakeOverrides& settings,
                                                         const hackeroot::BuildTarget target) {
    return startCommand(
        hackeroot::makeBuildCommand(root, settings, target, std::thread::hardware_concurrency()));
}

std::expected<void, base::Diagnostic>
BuildRunner::startCommand(const std::vector<std::string>& arguments) {
    if (isRunning()) {
        return std::unexpected(base::makeError({}, "a build is already running"));
    }
    if (worker.joinable()) {
        worker.join();
    }
    std::expected<Process, base::Diagnostic> started = Process::start(arguments);
    if (!started) {
        return std::unexpected(started.error());
    }
    log.append(std::format("$ {}", makeCommandLine(arguments)));
    process = std::make_unique<Process>(std::move(*started));
    status = BuildStatus::Running;
    stopRequested = false;
    startedAt = std::chrono::steady_clock::now();
    {
        const std::scoped_lock lock(mutex);
        pending.clear();
        finished = false;
        exitCode.reset();
    }
    worker = std::jthread([this, running = process.get()] { runWorker(*running); });
    return {};
}

void BuildRunner::stop() {
    if (isRunning() && process) {
        stopRequested = true;
        process->terminate();
    }
}

void BuildRunner::runWorker(Process& running) {
    LineDecoder decoder;
    while (const std::optional<std::string> chunk = running.readOutput()) {
        publish(decoder.decode(*chunk));
    }
    if (std::optional<std::string> rest = decoder.finish()) {
        publish({std::move(*rest)});
    }
    const std::optional<s32> code = running.wait();
    const std::scoped_lock lock(mutex);
    exitCode = code;
    finished = true;
}

void BuildRunner::publish(std::vector<std::string> lines) {
    if (lines.empty()) {
        return;
    }
    const std::scoped_lock lock(mutex);
    pending.insert(pending.end(), std::make_move_iterator(lines.begin()),
                   std::make_move_iterator(lines.end()));
}

std::optional<BuildOutcome> BuildRunner::update() {
    std::vector<std::string> arrived;
    bool ended = false;
    std::optional<s32> code;
    {
        const std::scoped_lock lock(mutex);
        arrived.swap(pending);
        ended = finished && isRunning();
        code = exitCode;
    }
    for (std::string& line : arrived) {
        log.append(std::move(line));
    }
    if (!ended) {
        return std::nullopt;
    }
    worker.join();
    process.reset();
    BuildOutcome outcome = finishBuild(code);
    log.append(outcome.summary);
    return outcome;
}

BuildOutcome BuildRunner::finishBuild(const std::optional<s32> code) {
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt);
    const std::string seconds = std::format("{:.1f} s", elapsed.count());
    if (stopRequested) {
        status = BuildStatus::Stopped;
        return {.status = status, .summary = std::format("Build stopped after {}", seconds)};
    }
    if (code == 0) {
        status = BuildStatus::Succeeded;
        return {.status = status, .summary = std::format("Build finished in {}", seconds)};
    }
    status = BuildStatus::Failed;
    if (!code) {
        return {.status = status, .summary = std::format("Build ended after {}", seconds)};
    }
    return {.status = status,
            .summary = std::format("make exited with code {} after {}", *code, seconds)};
}

} // namespace toki::build
