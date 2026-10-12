#include "EditWorker.h"

#include "hackeroot/CheckoutEdits.h"

#include <algorithm>
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
    ask(Job{.run = [makePlan = std::move(makePlan)] { return EditResult{.plan = makePlan()}; },
            .isPlan = true});
}

void EditWorker::apply(std::filesystem::path repository, hackeroot::EditPlan plan) {
    ask(Job{.run = [repository = std::move(repository), plan = std::move(plan)] {
        return EditResult{.applied = hackeroot::applyEditPlan(repository, plan)};
    }});
}

std::optional<EditResult> EditWorker::update() {
    const std::scoped_lock lock(mutex);
    if (finished.empty()) {
        return std::nullopt;
    }
    EditResult result = std::move(finished.front());
    finished.pop_front();
    return result;
}

bool EditWorker::isBusy() const {
    const std::scoped_lock lock(mutex);
    return !pending.empty() || isRunning;
}

void EditWorker::ask(Job job) {
    {
        const std::scoped_lock lock(mutex);
        if (job.isPlan) {
            std::erase_if(pending, [](const Job& waiting) { return waiting.isPlan; });
        }
        pending.push_back(std::move(job));
    }
    wake.notify_one();
}

void EditWorker::runWorker(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        Job job;
        {
            std::unique_lock lock(mutex);
            if (!wake.wait(lock, stop, [this] { return !pending.empty(); })) {
                return;
            }
            job = std::move(pending.front());
            pending.pop_front();
            isRunning = true;
        }
        EditResult result = job.run();
        const std::scoped_lock lock(mutex);
        finished.push_back(std::move(result));
        isRunning = false;
    }
}

} // namespace toki::tools
