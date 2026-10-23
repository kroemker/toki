#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace toki::build {

/// The command that opens `rom` in the emulator at `emulator`: the program and
/// the ROM's path, both with Windows separators, so that a ROM on a WSL share
/// is the `\\wsl.localhost\...` path any Windows program can open.
[[nodiscard]] std::vector<std::string> makeEmulatorCommand(const std::filesystem::path& emulator,
                                                           const std::filesystem::path& rom);

} // namespace toki::build
