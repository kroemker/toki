#pragma once

#include "app/tools/domain/EditWorker.h"
#include "hackeroot/CheckoutEdits.h"

#include <filesystem>
#include <optional>
#include <string>

namespace toki::tools {

/// How a tool's edit goes, from asking for it to its files written: the plan
/// is made on a worker, shown for review with what it adds, changes and warns
/// of, and written only once it is confirmed.
class EditFlow {
public:
    /// Makes the plan `makePlan` reads of the checkout at `repository`, to be
    /// reviewed and written there.
    void requestPlan(std::filesystem::path repository, EditWorker::MakePlan makePlan);

    /// Draws the plan under review, or how the last one went, while
    /// `repository` is the checkout open; a plan of another is dropped. True
    /// on the frame its files were written.
    [[nodiscard]] bool draw(const std::filesystem::path& repository);

    [[nodiscard]] bool isBusy() const {
        return editWorker.isBusy();
    }

    [[nodiscard]] bool isReviewing() const {
        return reviewed.has_value();
    }

private:
    [[nodiscard]] bool takeResult();
    void drawReview();

    EditWorker editWorker;
    std::filesystem::path plannedRepository;
    std::optional<hackeroot::EditPlan> reviewed;
    std::string status;
    bool isFailure = false;
};

} // namespace toki::tools
