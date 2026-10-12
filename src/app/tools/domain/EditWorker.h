#pragma once

#include "hackeroot/CheckoutEdits.h"

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

namespace toki::tools {

/// What a tool's worker finished: a plan made, or a plan applied.
struct EditResult {
    std::optional<std::expected<hackeroot::EditPlan, std::string>> plan;
    std::optional<std::expected<hackeroot::EditsApplied, std::string>> applied;
};

/// Makes a tool's edit plans of the checkout, and applies them, on a worker of
/// its own, since nothing on the UI thread reads the checkout. Jobs run one at
/// a time in the order asked, except that a plan asked for again before it has
/// started replaces it; a plan that is to be applied is never dropped, and
/// every result is kept until it is taken.
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

    /// The oldest result not taken yet, if there is one.
    [[nodiscard]] std::optional<EditResult> update();

    /// Whether a job is waiting or running.
    [[nodiscard]] bool isBusy() const;

private:
    struct Job {
        std::function<EditResult()> run;
        bool isPlan = false;
    };

    void ask(Job job);
    void runWorker(const std::stop_token& stop);

    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<Job> pending;
    std::deque<EditResult> finished;
    bool isRunning = false;
    std::jthread worker;
};

} // namespace toki::tools
