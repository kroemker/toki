#include "app/build/domain/EmulatorCommand.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32

TEST_CASE("the emulator is handed the ROM by its Windows path", "[build][emulator]") {
    const std::vector<std::string> command = toki::build::makeEmulatorCommand(
        R"(C:\Emulators\ares\ares.exe)",
        "//wsl.localhost/Ubuntu/home/leo/HackerOoT/build/ntsc-1.2/hackeroot-ntsc-1.2.z64");

    REQUIRE(
        command ==
        std::vector<std::string>{
            R"(C:\Emulators\ares\ares.exe)",
            R"(\\wsl.localhost\Ubuntu\home\leo\HackerOoT\build\ntsc-1.2\hackeroot-ntsc-1.2.z64)"});
}

#endif
