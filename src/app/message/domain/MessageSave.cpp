#include "MessageSave.h"

#include "app/message/domain/MessageEncoder.h"
#include "app/message/domain/MessageReader.h"
#include "app/message/domain/MessageWriter.h"
#include "cparser/SourceSet.h"
#include "hackeroot/CheckoutEdits.h"
#include "hackeroot/FeatureSwitches.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/TextLines.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::message {

namespace {

constexpr std::string_view VanillaInclude = "\"text/message_data.h\"";
constexpr std::string_view ConfigInclude = "\"config.h\"";
constexpr std::string_view RowMacro = "DEFINE_MESSAGE";

struct Directive {
    std::string_view keyword;
    std::string_view argument;
};

[[nodiscard]] std::optional<Directive> readDirective(const std::string_view line) {
    std::string_view rest = hackeroot::trimSpaces(line);
    if (!rest.starts_with('#')) {
        return std::nullopt;
    }
    rest = hackeroot::trimSpaces(rest.substr(1));
    const usize end = std::min(rest.find_first_of(" \t("), rest.size());
    std::string_view argument = hackeroot::trimSpaces(rest.substr(end));
    argument =
        hackeroot::trimSpaces(argument.substr(0, std::min(argument.find("//"), argument.size())));
    return Directive{.keyword = rest.substr(0, end), .argument = argument};
}

struct Conditional {
    std::string featureSwitch;
    bool isOn = true;
};

class SourceScanner {
public:
    SourceScanner(const IsSwitchOn& isSwitchOn, MessageSourceScan& scan)
        : isSwitchOn(isSwitchOn), scan(scan) {}

    void readLine(const std::string_view line, const usize number) {
        if (const std::optional<Directive> directive = readDirective(line)) {
            readDirectiveLine(*directive, number);
        } else if (line.contains(RowMacro) && !open.empty() && !open.back().isOn &&
                   !open.back().featureSwitch.empty()) {
            refuse(open.back().featureSwitch, number);
        }
        scan.switches.push_back(findSwitch());
    }

private:
    [[nodiscard]] std::string findSwitch() const {
        for (const Conditional& conditional : open | std::views::reverse) {
            if (!conditional.featureSwitch.empty()) {
                return conditional.featureSwitch;
            }
        }
        return {};
    }

    void refuse(const std::string& name, const usize number) {
        if (std::ranges::contains(refused, name)) {
            return;
        }
        refused.push_back(name);
        scan.refusals.push_back(std::format(
            "assets/text/message_data.h has messages under #if {}, from line {}, which is off, "
            "so they were never read and a save would lose them; turn the switch on in "
            "Configure, or take them out",
            name, number));
    }

    void readDirectiveLine(const Directive& directive, const usize number) {
        const std::string_view keyword = directive.keyword;
        if (keyword == "endif") {
            if (!open.empty()) {
                open.pop_back();
            }
            return;
        }
        if (keyword == "if" &&
            std::ranges::contains(hackeroot::FeatureSwitches, directive.argument)) {
            open.push_back(Conditional{.featureSwitch = std::string(directive.argument),
                                       .isOn = isSwitchOn(directive.argument)});
            return;
        }
        if (keyword == "if" || keyword == "ifdef" || keyword == "ifndef") {
            open.emplace_back();
        }
        if (keyword == "if" || keyword == "ifdef" || keyword == "ifndef" || keyword == "elif" ||
            keyword == "else") {
            scan.warnings.push_back(std::format(
                "line {}: #{} {} is not written again; the save keeps the rows the build reads "
                "under the configuration it has now",
                number, keyword, directive.argument));
            return;
        }
        if (keyword == "define" || keyword == "undef") {
            scan.warnings.push_back(std::format("line {}: #{} {} is not written again", number,
                                                keyword, directive.argument));
            return;
        }
        if (keyword == "include" && directive.argument != VanillaInclude &&
            directive.argument != ConfigInclude) {
            scan.warnings.push_back(std::format(
                "line {}: the rows of #include {} are written into the file in its place", number,
                directive.argument));
        }
    }

    const IsSwitchOn& isSwitchOn;
    MessageSourceScan& scan;
    std::vector<Conditional> open;
    std::vector<std::string> refused;
};

class SwitchFinder {
public:
    SwitchFinder(const std::filesystem::path& table, const MessageSourceScan& scan)
        : table(cparser::canonicalize(table)), scan(scan) {}

    [[nodiscard]] std::optional<std::string> find(const Message& message) {
        if (!isInTable(message.location.file) || message.location.line == 0 ||
            message.location.line > scan.switches.size()) {
            return std::nullopt;
        }
        const std::string& name = scan.switches[message.location.line - 1];
        return name.empty() ? std::nullopt : std::optional(name);
    }

private:
    [[nodiscard]] bool isInTable(const std::filesystem::path& file) {
        const std::string key = file.generic_string();
        const auto found = known.find(key);
        if (found != known.end()) {
            return found->second;
        }
        const bool inside = cparser::canonicalize(file) == table;
        known.emplace(key, inside);
        return inside;
    }

    std::filesystem::path table;
    const MessageSourceScan& scan;
    std::map<std::string, bool, std::less<>> known;
};

[[nodiscard]] std::optional<std::string> findDifference(const MessageTable& written,
                                                        const MessageTable& read) {
    if (written.messages.size() != read.messages.size()) {
        return std::format("{} messages were written and {} read back", written.messages.size(),
                           read.messages.size());
    }
    for (usize index = 0; index < written.messages.size(); ++index) {
        const Message& expected = written.messages[index];
        if (writeMessageRow(expected) != writeMessageRow(read.messages[index])) {
            return std::format("message 0x{:04X}, row {}, reads back differently", expected.id,
                               index + 1);
        }
    }
    return std::nullopt;
}

} // namespace

MessageSourceScan scanMessageSource(const std::string_view text, const IsSwitchOn& isSwitchOn) {
    MessageSourceScan scan;
    scan.isTokiFile = text.contains(hackeroot::makeTokiMarker("messages"));
    SourceScanner scanner(isSwitchOn, scan);
    const std::vector<std::string_view> lines = hackeroot::splitLines(text);
    for (usize index = 0; index < lines.size(); ++index) {
        scanner.readLine(lines[index], index + 1);
    }
    return scan;
}

std::expected<hackeroot::EditPlan, std::string>
planMessageSave(const hackeroot::Workspace& workspace, const MessageTable& table) {
    const std::filesystem::path& root = workspace.getRoot();
    const MessageFiles files = findMessageFiles(root, workspace.getBuildDefines().version);
    std::expected<std::string, std::string> before = hackeroot::readCheckoutText(files.table);
    if (!before) {
        return std::unexpected(before.error());
    }
    const std::vector<std::string> configInclude{"config.h"};
    const hackeroot::Prelude config = hackeroot::readPrelude(workspace, configInclude);
    const MessageSourceScan scan = scanMessageSource(
        *before, [&](const std::string_view name) { return isConditionTrue(config.macros, name); });
    if (!scan.refusals.empty()) {
        std::string refused;
        for (const std::string& refusal : scan.refusals) {
            refused += (refused.empty() ? "" : "; ") + refusal;
        }
        return std::unexpected(refused);
    }

    SwitchFinder switchFinder(files.table, scan);
    const std::string after =
        writeMessageFile(table, [&](const Message& message) { return switchFinder.find(message); });

    const cparser::PendingFiles pending{{cparser::canonicalize(files.table), after}};
    const MessageTable readBack = readMessageTable(workspace.getMacros(), files, &pending);
    if (const std::optional<std::string> difference = findDifference(table, readBack)) {
        return std::unexpected(std::format(
            "the messages would not read back as edited ({}), so nothing is written", *difference));
    }

    hackeroot::FileEdit edit{
        .file = files.table.lexically_relative(root), .before = std::move(*before), .after = after};
    edit.items.push_back(hackeroot::EditItem{
        .status = hackeroot::EditStatus::Update,
        .label = std::format("{} messages, written whole", table.messages.size())});
    if (!scan.isTokiFile) {
        edit.items.push_back(hackeroot::EditItem{
            .status = hackeroot::EditStatus::Add,
            .label = std::format("the vanilla messages of {}, in place of the #include of them",
                                 (files.vanillaDirectory / "message_data.h")
                                     .lexically_relative(root)
                                     .generic_string())});
    }
    hackeroot::EditPlan plan;
    plan.files.push_back(std::move(edit));
    plan.warnings = scan.warnings;
    return plan;
}

} // namespace toki::message
