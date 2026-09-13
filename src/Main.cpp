#include "app/core/ui/App.h"

#include <cstdio>
#include <exception>
#include <expected>
#include <print>
#include <string>

int main() {
    // Nothing here throws, but vcpkg builds toml++ and SDL with exceptions on, and an
    // escaped one would otherwise abort without a word. The handlers use the C API
    // because std::println is itself allowed to throw.
    try {
        toki::core::App app;

        if (const std::expected<void, std::string> ready = app.initialize(); !ready) {
            std::println(stderr, "{}", ready.error());
            return 1;
        }

        app.run();
    } catch (const std::exception& error) {
        std::fputs("unhandled exception: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("unhandled exception\n", stderr);
        return 1;
    }
    return 0;
}
