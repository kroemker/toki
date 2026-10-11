#include "ProblemText.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ReadableName.h"

#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace toki::actor {

namespace {

constexpr std::string_view PlaceSeparator = " › ";

struct NamedStep {
    std::string_view key;
    std::string_view name;
};

constexpr std::array<NamedStep, 3> IndexedSteps{{
    {.key = "variants", .name = "Variant"},
    {.key = "parts", .name = "Part"},
    {.key = "children", .name = "Child"},
}};

[[nodiscard]] std::string makeIndexedStep(const std::string_view step) {
    const usize open = step.find('[');
    const std::string_view key = step.substr(0, open);
    usize index = 0;
    const std::string_view digits = step.substr(open + 1, step.size() - open - 2);
    std::from_chars(digits.data(), digits.data() + digits.size(), index);
    for (const NamedStep& named : IndexedSteps) {
        if (named.key == key) {
            return std::format("{} {}", named.name, index + 1);
        }
    }
    return std::format("{} {}", makeReadableName(key), index + 1);
}

[[nodiscard]] std::string makePlace(std::string_view path) {
    std::string place;
    const auto append = [&place](const std::string& step) {
        place += (place.empty() ? "" : std::string(PlaceSeparator)) + step;
    };
    while (!path.empty()) {
        const usize dot = path.find('.');
        const std::string_view step = path.substr(0, dot);
        const std::string_view rest = dot == std::string_view::npos ? "" : path.substr(dot + 1);
        if (step == "params" || step == "match") {
            append(std::format("{} \"{}\"", step == "params" ? "Param" : "Condition on", rest));
            break;
        }
        append(step.ends_with(']') && step.contains('[') ? makeIndexedStep(step)
                                                         : makeReadableName(step));
        path = rest;
    }
    return place;
}

[[nodiscard]] std::string makeSentence(const std::string_view text) {
    std::string sentence(text);
    if (!sentence.empty()) {
        sentence.front() =
            static_cast<char>(std::toupper(static_cast<unsigned char>(sentence.front())));
    }
    return sentence;
}

} // namespace

ProblemText makeProblemText(const std::string_view actor, std::string_view message) {
    const std::string prefix = std::format("{}.", actor);
    if (!message.starts_with(prefix)) {
        return ProblemText{.message = makeSentence(message)};
    }
    message.remove_prefix(prefix.size());
    const usize colon = message.find(": ");
    if (colon == std::string_view::npos) {
        return ProblemText{.message = makeSentence(message)};
    }
    return ProblemText{.place = makePlace(message.substr(0, colon)),
                       .message = makeSentence(message.substr(colon + 2))};
}

} // namespace toki::actor
