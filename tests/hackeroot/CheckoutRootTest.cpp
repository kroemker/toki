#include "base/Diagnostic.h"
#include "hackeroot/Checkout.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>

namespace {

[[nodiscard]] std::filesystem::path getCheckout() {
    return std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout";
}

} // namespace

TEST_CASE("a checkout is its own root", "[hackeroot][checkout]") {
    const std::expected<std::filesystem::path, base::Diagnostic> root =
        hackeroot::findCheckoutRoot(getCheckout());

    REQUIRE(root.has_value());
    REQUIRE(std::filesystem::equivalent(*root, getCheckout()));
}

TEST_CASE("any folder inside a checkout finds its root", "[hackeroot][checkout]") {
    const std::expected<std::filesystem::path, base::Diagnostic> root =
        hackeroot::findCheckoutRoot(getCheckout() / "include/tables");

    REQUIRE(root.has_value());
    REQUIRE(std::filesystem::equivalent(*root, getCheckout()));
}

TEST_CASE("a folder outside any checkout says what it lacks", "[hackeroot][checkout]") {
    const std::expected<std::filesystem::path, base::Diagnostic> root =
        hackeroot::findCheckoutRoot(getCheckout().parent_path());

    REQUIRE_FALSE(root.has_value());
    REQUIRE(root.error().message.contains("Makefile"));
    REQUIRE(root.error().message.contains("include/config.h"));
}

TEST_CASE("a folder that does not exist is an error, not a crash", "[hackeroot][checkout]") {
    REQUIRE_FALSE(hackeroot::findCheckoutRoot(getCheckout() / "nowhere").has_value());
}
