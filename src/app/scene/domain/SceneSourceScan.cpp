#include "SceneSourceScan.h"

#include "app/scene/Types.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/FeatureSwitches.h"
#include "hackeroot/TextLines.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::scene {

namespace {

constexpr std::string_view LengthPrefix = "LENGTH_";
constexpr usize NamedLimit = 3;

struct Directive {
    std::string_view keyword;
    std::string_view argument;
};

std::optional<Directive> readDirective(const std::string_view line) {
    std::string_view rest = hackeroot::trimSpaces(line);
    if (!rest.starts_with('#')) {
        return std::nullopt;
    }
    rest = hackeroot::trimSpaces(rest.substr(1));
    const usize end = std::min(rest.find_first_of(" \t("), rest.size());
    std::string_view argument = hackeroot::trimSpaces(rest.substr(end));
    argument = argument.substr(0, std::min(argument.find("//"), argument.size()));
    return Directive{.keyword = rest.substr(0, end), .argument = hackeroot::trimSpaces(argument)};
}

std::string_view readName(const std::string_view argument) {
    return argument.substr(0, std::min(argument.find_first_of(" \t("), argument.size()));
}

std::string listNames(const std::vector<std::string>& names) {
    std::string listed;
    for (usize i = 0; i < std::min(names.size(), NamedLimit); ++i) {
        listed += std::format("{}{}", i == 0 ? "" : ", ", names[i]);
    }
    if (names.size() > NamedLimit) {
        listed += std::format(" and {} more", names.size() - NamedLimit);
    }
    return listed;
}

class FileScanner {
public:
    FileScanner(const SceneSource& source, const IsSwitchOn& isSwitchOn, SceneSourceScan& scan)
        : source(source), isSwitchOn(isSwitchOn), scan(scan) {}

    void scanFile();

private:
    void readGuard(const std::vector<std::string_view>& lines);
    void readConditional(const Directive& directive);

    struct Conditional {
        bool isSwitch = false;
        bool isGuard = false;
    };

    const SceneSource& source;
    const IsSwitchOn& isSwitchOn;
    SceneSourceScan& scan;
    std::string guard;
    std::vector<Conditional> open;
    std::vector<std::string> conditions;
};

void FileScanner::readGuard(const std::vector<std::string_view>& lines) {
    std::vector<Directive> directives;
    for (const std::string_view line : lines) {
        if (std::optional<Directive> directive = readDirective(line)) {
            directives.push_back(*directive);
            if (directives.size() == 2) {
                break;
            }
        }
    }
    if (directives.size() == 2 && directives[0].keyword == "ifndef" &&
        directives[1].keyword == "define" &&
        readName(directives[1].argument) == directives[0].argument) {
        guard = std::string(directives[0].argument);
    }
}

void FileScanner::readConditional(const Directive& directive) {
    if (directive.keyword == "endif") {
        if (!open.empty()) {
            open.pop_back();
        }
        return;
    }
    if (directive.keyword == "else" || directive.keyword == "elif") {
        if (!open.empty() && open.back().isGuard) {
            return;
        }
        conditions.push_back(std::format("#{}", directive.keyword));
        return;
    }
    const std::string_view name = readName(directive.argument);
    if (directive.keyword == "ifndef" && !guard.empty() && name == guard && open.empty()) {
        open.push_back(Conditional{.isGuard = true});
        return;
    }
    if (directive.keyword == "if" && name == directive.argument &&
        std::ranges::contains(hackeroot::FeatureSwitches, name)) {
        open.push_back(Conditional{.isSwitch = true});
        if (!isSwitchOn(name)) {
            scan.refusals.push_back(std::format(
                "{} has data under #if {}, which is off, so it is in no image and a save would "
                "lose it; turn the switch on in Configure, or take the data out",
                source.file.generic_string(), name));
        }
        return;
    }
    open.push_back(Conditional{});
    conditions.push_back(std::format("#{} {}", directive.keyword, directive.argument));
}

void FileScanner::scanFile() {
    const std::vector<std::string_view> lines = hackeroot::splitLines(source.text);
    readGuard(lines);
    if (source.text.contains(hackeroot::makeTokiMarker("scene"))) {
        scan.isTokiScene = true;
    }
    for (const std::string_view line : lines) {
        const std::optional<Directive> directive = readDirective(line);
        if (!directive) {
            continue;
        }
        if (directive->keyword == "if" || directive->keyword == "ifdef" ||
            directive->keyword == "ifndef" || directive->keyword == "elif" ||
            directive->keyword == "else" || directive->keyword == "endif") {
            readConditional(*directive);
            continue;
        }
        if (directive->keyword != "define" && directive->keyword != "undef") {
            continue;
        }
        const std::string_view name = readName(directive->argument);
        if (directive->keyword == "define" && name == guard) {
            continue;
        }
        if (directive->keyword == "define" && name.starts_with(LengthPrefix)) {
            scan.lengthDefines.emplace(name.substr(LengthPrefix.size()));
            continue;
        }
        scan.defines.push_back(SourceDefine{.file = source.file,
                                            .name = std::string(name),
                                            .line = directive->keyword == "define"
                                                        ? std::string(hackeroot::trimSpaces(line))
                                                        : std::string()});
    }
    if (!conditions.empty()) {
        scan.warnings.push_back(
            std::format("{} has {}; a save keeps only what the build reads under the "
                        "configuration it has now",
                        source.file.generic_string(), listNames(conditions)));
    }
}

} // namespace

SceneSourceScan scanSceneSources(const std::span<const SceneSource> sources,
                                 const IsSwitchOn& isSwitchOn) {
    SceneSourceScan scan;
    for (const SceneSource& source : sources) {
        FileScanner(source, isSwitchOn, scan).scanFile();
    }
    return scan;
}

} // namespace toki::scene
