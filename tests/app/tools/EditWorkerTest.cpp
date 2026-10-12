#include "app/tools/domain/EditWorker.h"

#include "hackeroot/CheckoutEdits.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

namespace {

[[nodiscard]] std::optional<toki::tools::EditResult> waitFor(toki::tools::EditWorker& worker) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
        if (std::optional<toki::tools::EditResult> result = worker.update()) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("a tool's plan is made and applied on the worker", "[tools]") {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / "toki_tests" / "edit_worker";
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    toki::tools::EditWorker worker;

    worker.plan([] {
        hackeroot::EditPlan plan;
        plan.files.push_back(hackeroot::FileEdit{.file = "made.h", .after = "#define A 1\n"});
        return std::expected<hackeroot::EditPlan, std::string>(plan);
    });
    const std::optional<toki::tools::EditResult> waited = waitFor(worker);
    REQUIRE(waited.has_value());
    const toki::tools::EditResult& planned = *waited;
    REQUIRE(planned.plan.has_value());
    REQUIRE(planned.plan->has_value());
    REQUIRE((*planned.plan)->hasWork());

    worker.apply(folder, **planned.plan);
    const std::optional<toki::tools::EditResult> appliedResult = waitFor(worker);
    REQUIRE(appliedResult.has_value());
    const toki::tools::EditResult& applied = *appliedResult;
    REQUIRE(applied.applied.has_value());
    REQUIRE(applied.applied->has_value());
    REQUIRE(std::filesystem::exists(folder / "made.h"));
    REQUIRE_FALSE(worker.isBusy());
    std::filesystem::remove_all(folder, ignored);
}
