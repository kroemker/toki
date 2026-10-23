#include "EmulatorCommand.h"

#include <filesystem>
#include <string>
#include <vector>

namespace toki::build {

namespace {

[[nodiscard]] std::string encodeNative(std::filesystem::path path) {
    const std::u8string encoded = path.make_preferred().u8string();
    return {encoded.begin(), encoded.end()};
}

} // namespace

std::vector<std::string> makeEmulatorCommand(const std::filesystem::path& emulator,
                                             const std::filesystem::path& rom) {
    return {encodeNative(emulator), encodeNative(rom)};
}

} // namespace toki::build
