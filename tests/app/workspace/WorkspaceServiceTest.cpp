#include "app/workspace/domain/WorkspaceService.h"

#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

namespace {

constexpr std::chrono::milliseconds PollInterval(50);
constexpr std::chrono::seconds Patience(30);

/// A copy of the fixture checkout in the temp directory, so a test can edit it.
class CheckoutCopy {
public:
    explicit CheckoutCopy(const std::string& name)
        : root(std::filesystem::temp_directory_path() / "toki_tests" / name) {
        std::error_code failure;
        std::filesystem::remove_all(root, failure);
        std::filesystem::create_directories(root.parent_path(), failure);
        std::filesystem::copy(std::filesystem::path(TOKI_TEST_FIXTURES) /
                                  "hackeroot/fixtures/checkout",
                              root, std::filesystem::copy_options::recursive, failure);
    }

    CheckoutCopy(const CheckoutCopy&) = delete;
    CheckoutCopy(CheckoutCopy&&) = delete;
    CheckoutCopy& operator=(const CheckoutCopy&) = delete;
    CheckoutCopy& operator=(CheckoutCopy&&) = delete;

    ~CheckoutCopy() {
        std::error_code failure;
        std::filesystem::remove_all(root, failure);
    }

    [[nodiscard]] const std::filesystem::path& getRoot() const {
        return root;
    }

    void replaceIn(const std::filesystem::path& file, const std::string& from,
                   const std::string& to) const {
        const std::filesystem::path path = root / file;
        std::ostringstream contents;
        contents << std::ifstream(path).rdbuf();
        std::string text = contents.str();
        text.replace(text.find(from), from.size(), to);
        std::ofstream(path, std::ios::trunc) << text;
        std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) +
                                                   std::chrono::hours(1));
    }

private:
    std::filesystem::path root;
};

[[nodiscard]] bool waitForUpdate(toki::workspace::WorkspaceService& service) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + Patience;
    while (std::chrono::steady_clock::now() < deadline) {
        if (service.update()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

[[nodiscard]] bool waitUntilIdle(const toki::workspace::WorkspaceService& service) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + Patience;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!service.isLoading()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

TEST_CASE("opening a checkout reads it off the calling thread", "[workspace][service]") {
    const CheckoutCopy checkout("service_open");
    toki::workspace::WorkspaceService service(PollInterval);
    REQUIRE(service.getWorkspace() == nullptr);

    service.open(checkout.getRoot(), {});

    REQUIRE(waitForUpdate(service));
    REQUIRE(service.getWorkspace() != nullptr);
    REQUIRE(service.getWorkspace()->getTables().scenes.size() == 3);
}

TEST_CASE("editing a configuration header reads the checkout again on its own",
          "[workspace][service]") {
    const CheckoutCopy checkout("service_watch");
    toki::workspace::WorkspaceService service(PollInterval);
    service.open(checkout.getRoot(), {});
    REQUIRE(waitForUpdate(service));
    REQUIRE(service.getWorkspace()->getTables().scenes.size() == 3);

    checkout.replaceIn("include/config/config_debug.h", "INCLUDE_EXAMPLE_SCENE true",
                       "INCLUDE_EXAMPLE_SCENE false");

    REQUIRE(waitForUpdate(service));
    REQUIRE(service.getWorkspace()->getTables().scenes.size() == 2);
}

TEST_CASE("different build settings read the checkout as that build", "[workspace][service]") {
    const CheckoutCopy checkout("service_settings");
    toki::workspace::WorkspaceService service(PollInterval);

    service.open(checkout.getRoot(), {{"VERSION", "gc-eu-mq-dbg"}});

    REQUIRE(waitForUpdate(service));
    REQUIRE(service.getWorkspace()->getTables().scenes.size() == 4);
}

TEST_CASE("a checkout that cannot be read keeps the workspace that was open",
          "[workspace][service]") {
    const CheckoutCopy checkout("service_failure");
    toki::workspace::WorkspaceService service(PollInterval);
    service.open(checkout.getRoot(), {});
    REQUIRE(waitForUpdate(service));
    const hackeroot::Workspace* before = service.getWorkspace();

    service.open(checkout.getRoot() / "include", {});

    REQUIRE(waitUntilIdle(service));
    REQUIRE_FALSE(service.update());
    REQUIRE(service.getWorkspace() == before);
}
