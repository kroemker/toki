#include "ActorDatabase.h"
#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "base/Diagnostic.h"

#include <algorithm>
#include <format>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

class EntryChecker {
public:
    explicit EntryChecker(const ActorEntry& entry) : entry(entry) {}

    [[nodiscard]] std::vector<base::Diagnostic> check() && {
        for (const ActorParam& param : entry.params) {
            checkParam(param);
        }
        checkOverlaps();
        for (usize i = 0; i < entry.variants.size(); ++i) {
            checkVariant(entry.variants[i], i);
        }
        return std::move(diagnostics);
    }

private:
    void report(const base::SourceLocation& location, const std::string_view path,
                const std::string_view message) {
        diagnostics.push_back(
            base::makeWarning(location, std::format("{}.{}: {}", entry.actor, path, message)));
    }

    void checkParam(const ActorParam& param);
    void checkOverlaps();
    void checkVariant(const ActorVariant& variant, usize index);
    void checkMatch(const ActorVariant& variant, const ParamMatch& match, std::string_view path);
    void checkPart(const ActorPart& part, std::string_view path);

    const ActorEntry& entry;
    std::vector<base::Diagnostic> diagnostics;
};

void EntryChecker::checkParam(const ActorParam& param) {
    const std::string path = std::format("params.{}", param.name);
    if (isBuiltInParam(param.name)) {
        report(param.location, path,
               std::format("'{}' is a param every actor has, and cannot be declared", param.name));
    }
    if (param.mask == 0) {
        report(param.location, path, "has no bits");
        return;
    }
    if (!isMaskContiguous(param.mask)) {
        report(param.location, path,
               std::format("the bits of mask 0x{:04X} are not next to each other", param.mask));
    }
    const u8 width = findMaskWidth(param.mask);
    if (param.kind == ParamKind::Flag && width != 1) {
        report(param.location, path, "a flag is one bit");
    }
    if (param.kind == ParamKind::Choice && param.values.empty() && !param.collection) {
        report(param.location, path, "a choice needs values");
    }
    if (param.kind != ParamKind::Choice && param.collection) {
        report(param.location, path, "only a choice takes its values from a collection");
    }
    const s64 lowest = param.isSigned ? -(s64{1} << (width - 1)) : 0;
    const s64 highest = param.isSigned ? (s64{1} << (width - 1)) - 1 : (s64{1} << width) - 1;
    std::set<s64> seen;
    for (const ChoiceValue& value : param.values) {
        if (value.value < lowest || value.value > highest) {
            report(
                param.location, path,
                std::format("{} ('{}') does not fit in {} bits", value.value, value.label, width));
        }
        if (!seen.insert(value.value).second) {
            report(param.location, path, std::format("{} is labelled twice", value.value));
        }
    }
}

void EntryChecker::checkOverlaps() {
    for (usize i = 0; i < entry.params.size(); ++i) {
        for (usize j = i + 1; j < entry.params.size(); ++j) {
            const ActorParam& first = entry.params[i];
            const ActorParam& second = entry.params[j];
            if (first.source == second.source && (first.mask & second.mask) != 0) {
                report(first.location, std::format("params.{}", first.name),
                       std::format("shares bits with '{}'", second.name));
            }
        }
    }
}

void EntryChecker::checkVariant(const ActorVariant& variant, const usize index) {
    const std::string path = std::format("variants[{}]", index);
    for (const ParamMatch& match : variant.match) {
        checkMatch(variant, match, path);
    }
    for (usize i = 0; i < variant.parts.size(); ++i) {
        checkPart(variant.parts[i], std::format("{}.parts[{}]", path, i));
    }
    if (variant.match.empty() && index + 1 < entry.variants.size()) {
        report(variant.location, path,
               "matches every placement, so the variants after it are never drawn");
    }
}

void EntryChecker::checkMatch(const ActorVariant& variant, const ParamMatch& match,
                              const std::string_view path) {
    const std::string at = std::format("{}.match.{}", path, match.param);
    if (match.values.empty()) {
        report(variant.location, at, "has no values, so the variant matches no placement");
    }
    if (isBuiltInParam(match.param)) {
        for (const MatchValue& value : match.values) {
            const std::string* name = std::get_if<std::string>(&value);
            if (name == nullptr || (match.param == "age" && *name != "child" && *name != "adult")) {
                report(variant.location, at,
                       match.param == "age" ? "an age is \"child\" or \"adult\""
                                            : "a scene is matched by its enum name");
            }
        }
        return;
    }
    const auto param = std::ranges::find(entry.params, match.param, &ActorParam::name);
    if (param == entry.params.end()) {
        report(variant.location, at, "is not a param of the actor");
        return;
    }
    for (const MatchValue& value : match.values) {
        if (std::holds_alternative<std::string>(value)) {
            report(variant.location, at,
                   "a param is matched by a number, or a flag by true or false");
        } else if (std::holds_alternative<bool>(value) != (param->kind == ParamKind::Flag)) {
            report(variant.location, at,
                   param->kind == ParamKind::Flag ? "a flag matches true or false"
                                                  : "only a flag matches true or false");
        } else if (const s64* number = std::get_if<s64>(&value);
                   number != nullptr && param->kind == ParamKind::Choice && !param->collection &&
                   !std::ranges::contains(param->values, *number + param->offset,
                                          &ChoiceValue::value)) {
            report(variant.location, at, std::format("{} is not one of its values", *number));
        }
    }
}

void EntryChecker::checkPart(const ActorPart& part, const std::string_view path) {
    if (part.symbol.empty()) {
        report(part.location, path, "draws nothing: it names no display list or skeleton");
    }
    if (part.kind != PartKind::Skeleton && (part.pose || part.poseObject)) {
        report(part.location, path, "only a skeleton stands in a pose");
    }
}

} // namespace

std::vector<base::Diagnostic> checkActorEntry(const ActorEntry& entry) {
    return EntryChecker(entry).check();
}

} // namespace toki::actor
