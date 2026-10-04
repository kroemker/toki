#include "BuildCommand.h"

#include "hackeroot/Types.h"
#include "hackeroot/WslPath.h"
#include "makeparser/Makefile.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace hackeroot {

namespace {

std::string encodeUtf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return {encoded.begin(), encoded.end()};
}

} // namespace

std::vector<std::string> makeBuildCommand(const std::filesystem::path& root,
                                          const makeparser::MakeOverrides& settings,
                                          const BuildTarget target, const u32 jobs) {
    std::vector<std::string> command{"wsl.exe"};
    if (const std::optional<WslPath> wslPath = findWslPath(root)) {
        command.insert(command.end(),
                       {"--distribution", wslPath->distribution, "--cd", wslPath->linuxPath});
    } else {
        command.insert(command.end(), {"--cd", encodeUtf8(root)});
    }
    command.insert(command.end(), {"--exec", "make", std::format("-j{}", std::max<u32>(jobs, 1))});
    for (const auto& [name, value] : settings) {
        command.push_back(std::format("{}={}", name, value));
    }
    if (target == BuildTarget::Run) {
        command.emplace_back("run");
    }
    return command;
}

} // namespace hackeroot
