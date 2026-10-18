#pragma once

#include <filesystem>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

/// One file of a scene as it was read, before a save replaces it.
struct SceneSource {
    std::filesystem::path file;
    std::string text;
};

/// A `#define` of a scene's file, as its line reads.
struct SourceDefine {
    std::filesystem::path file;
    std::string name;
    std::string line;
};

/// What a scene's files hold that its images do not, and so what a save of
/// it writes again, loses, or cannot write at all.
struct SceneSourceScan {
    /// Symbols a `#define LENGTH_<symbol>` gave a length, which a save writes
    /// again.
    std::set<std::string, std::less<>> lengthDefines;
    /// Every other `#define` and `#undef`, the include guards apart, for the
    /// save to keep or say it loses.
    std::vector<SourceDefine> defines;
    /// What a save would lose: a file's `#if`s, the include guards apart.
    std::vector<std::string> warnings;
    /// Why the scene cannot be saved: data under a HackerOoT feature switch
    /// that is off, which no image holds.
    std::vector<std::string> refusals;
    /// Whether a file carries `// toki: scene`, so Toki wrote it.
    bool isTokiScene = false;
};

/// Whether a HackerOoT feature switch is on under the checkout's configuration.
using IsSwitchOn = std::function<bool(std::string_view name)>;

/// Reads the directives of a scene's files for what a save of it does with
/// them.
[[nodiscard]] SceneSourceScan scanSceneSources(std::span<const SceneSource> sources,
                                               const IsSwitchOn& isSwitchOn);

} // namespace toki::scene
