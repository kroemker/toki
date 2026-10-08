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

constexpr u8 SourceBits = 16;

class EntryChecker {
public:
    explicit EntryChecker(const ActorEntry& entry) : entry(entry) {}

    [[nodiscard]] std::vector<base::Diagnostic> check() && {
        for (const ParamField& field : entry.fields) {
            checkField(field);
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

    void checkField(const ParamField& field);
    void checkOverlaps();
    void checkVariant(const ActorVariant& variant, usize index);
    void checkMatch(const ActorVariant& variant, const FieldMatch& match, std::string_view path);

    const ActorEntry& entry;
    std::vector<base::Diagnostic> diagnostics;
};

void EntryChecker::checkField(const ParamField& field) {
    const std::string path = std::format("fields.{}", field.name);
    if (field.name == "age") {
        report(field.location, path, "'age' is a field every actor has, and cannot be declared");
    }
    if (field.width == 0 || field.shift + field.width > SourceBits) {
        report(field.location, path,
               std::format("bits {} to {} do not fit in 16", field.shift,
                           field.shift + field.width - 1));
        return;
    }
    if (field.kind == FieldKind::Flag && field.width != 1) {
        report(field.location, path, "a flag is one bit wide");
    }
    if (field.kind == FieldKind::Enum && field.values.empty()) {
        report(field.location, path, "an enum needs values");
    }
    const s64 lowest = field.isSigned ? -(s64{1} << (field.width - 1)) : 0;
    const s64 highest =
        field.isSigned ? (s64{1} << (field.width - 1)) - 1 : (s64{1} << field.width) - 1;
    std::set<std::string_view> names;
    for (const EnumValue& value : field.values) {
        if (value.value < lowest || value.value > highest) {
            report(field.location, path,
                   std::format("'{}' is {}, which does not fit in {} bits", value.name, value.value,
                               field.width));
        }
        if (!names.insert(value.name).second) {
            report(field.location, path, std::format("'{}' is named twice", value.name));
        }
    }
}

void EntryChecker::checkOverlaps() {
    for (usize i = 0; i < entry.fields.size(); ++i) {
        for (usize j = i + 1; j < entry.fields.size(); ++j) {
            const ParamField& first = entry.fields[i];
            const ParamField& second = entry.fields[j];
            const bool overlaps = first.source == second.source &&
                                  first.shift < second.shift + second.width &&
                                  second.shift < first.shift + first.width;
            if (overlaps) {
                report(second.location, std::format("fields.{}", second.name),
                       std::format("shares bits with '{}'", first.name));
            }
        }
    }
}

void EntryChecker::checkVariant(const ActorVariant& variant, const usize index) {
    const std::string path = std::format("variants[{}]", index);
    for (const FieldMatch& match : variant.match) {
        checkMatch(variant, match, path);
    }
    if (variant.skeleton && !variant.displayLists.empty()) {
        report(variant.location, path, "draws a skeleton and display lists both");
    }
    if (variant.animation && !variant.skeleton) {
        report(variant.location, path, "has an animation but no skeleton");
    }
    if (variant.match.empty() && index + 1 < entry.variants.size()) {
        report(variant.location, path,
               "matches every placement, so the variants after it are never drawn");
    }
}

void EntryChecker::checkMatch(const ActorVariant& variant, const FieldMatch& match,
                              const std::string_view path) {
    const std::string at = std::format("{}.match.{}", path, match.field);
    if (match.field == "age") {
        const std::string* age = std::get_if<std::string>(&match.value);
        if (age == nullptr || (*age != "child" && *age != "adult")) {
            report(variant.location, at, "an age is \"child\" or \"adult\"");
        }
        return;
    }
    const auto field = std::ranges::find(entry.fields, match.field, &ParamField::name);
    if (field == entry.fields.end()) {
        report(variant.location, at, "is not a field of the actor");
        return;
    }
    if (std::holds_alternative<bool>(match.value) != (field->kind == FieldKind::Flag)) {
        report(variant.location, at,
               field->kind == FieldKind::Flag ? "a flag matches true or false"
                                              : "only a flag matches true or false");
        return;
    }
    const std::string* name = std::get_if<std::string>(&match.value);
    if (name == nullptr) {
        return;
    }
    if (field->kind != FieldKind::Enum) {
        report(variant.location, at, "only an enum's values have names");
    } else if (!std::ranges::contains(field->values, *name, &EnumValue::name)) {
        report(variant.location, at, std::format("'{}' is not a value of the field", *name));
    }
}

} // namespace

std::vector<base::Diagnostic> checkActorEntry(const ActorEntry& entry) {
    return EntryChecker(entry).check();
}

} // namespace toki::actor
