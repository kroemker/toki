#include "EditWorker.h"

#include "hackeroot/CheckoutEdits.h"

#include <expected>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace toki::tools {

EditWorker::EditWorker() : worker([this](const std::stop_token& stop) { runWorker(stop); }) {}

void EditWorker::plan(MakePlan makePlan) {
    ask([makePlan = std::move(makePlan)] { return EditResult{.plan = makePlan()}; });
}

void EditWorker::apply(std::filesystem::path repository, hackeroot::EditPlan plan) {
    ask([repository = std::move(repository), plan = std::move(plan)] {
        return EditResult{.applied = hackeroot::applyEditPlan(repository, plan)};
    });
}

std::optional<EditResult> EditWorker::update() {
    const std::scoped_lock lock(mutex);
    return std::exchange(finished, std::nullopt);
}

bool EditWorker::isBusy() const {
    const std::scoped_lock lock(mutex);
    return pending.has_value() || isRunning;
}

void EditWorker::ask(Job job) {
    {
        const std::scoped_lock lock(mutex);
        pending = std::move(job);
    }
    wake.notify_one();
}

void EditWorker::runWorker(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        Job job;
        {
            std::unique_lock lock(mutex);
            if (!wake.wait(lock, stop, [this] { return pending.has_value(); })) {
                return;
            }
            job = std::move(*pending);
            pending.reset();
            isRunning = true;
        }
        EditResult result = job();
        const std::scoped_lock lock(mutex);
        finished = std::move(result);
        isRunning = false;
    }
}

} // namespace toki::tools
