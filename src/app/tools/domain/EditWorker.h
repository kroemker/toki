#pragma once

#include "hackeroot/CheckoutEdits.h"

#include <condition_variable>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace toki::tools {

/// What a tool's worker finished: a plan made, or a plan applied.
struct EditResult {
    std::optional<std::expected<hackeroot::EditPlan, std::string>> plan;
    std::optional<std::expected<hackeroot::EditsApplied, std::string>> applied;
};

/// Makes a tool's edit plans of the checkout, and applies them, on a worker of
/// its own, since nothing on the UI thread reads the checkout. One job runs at
/// a time; asking again before it has started replaces it.
///
/// Every member function is called from the UI thread.
class EditWorker {
public:
    /// Reads the checkout and says what a tool would change in it.
    using MakePlan = std::function<std::expected<hackeroot::EditPlan, std::string>()>;

    EditWorker();
    EditWorker(const EditWorker&) = delete;
    EditWorker& operator=(const EditWorker&) = delete;
    EditWorker(EditWorker&&) = delete;
    EditWorker& operator=(EditWorker&&) = delete;
    ~EditWorker() = default;

    /// Makes a plan with `makePlan`.
    void plan(MakePlan makePlan);

    /// Writes `plan` to the checkout at `repository` (`hackeroot::applyEditPlan`).
    void apply(std::filesystem::path repository, hackeroot::EditPlan plan);

    /// What finished since the last call, if anything did.
    [[nodiscard]] std::optional<EditResult> update();

    /// Whether a job is waiting or running.
    [[nodiscard]] bool isBusy() const;

private:
    using Job = std::function<EditResult()>;

    void ask(Job job);
    void runWorker(const std::stop_token& stop);

    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::optional<Job> pending;
    std::optional<EditResult> finished;
    bool isRunning = false;
    std::jthread worker;
};

} // namespace toki::tools
