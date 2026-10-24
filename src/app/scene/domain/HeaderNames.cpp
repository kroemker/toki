#include "HeaderNames.h"

#include "app/core/domain/ReadableName.h"
#include "app/scene/Types.h"
#include "app/scene/domain/HeaderSettings.h"
#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/TypeTable.h"
#include "cparser/Value.h"
#include "z64/DecompDialect.h"

#include <algorithm>
#include <array>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::scene {

namespace {

constexpr std::string_view CameraTypePrefix = "SCENE_CAM_TYPE_";

struct NamedSetting {
    HeaderSetting setting;
    std::string_view enumeration;
};

constexpr std::array<NamedSetting, 9> Enumerations{{
    {HeaderSetting::NatureAmbience, "NatureAmbienceId"},
    {HeaderSetting::Sequence, "NA_BGM"},
    {HeaderSetting::Skybox, "SkyboxId"},
    {HeaderSetting::LightMode, "LightMode"},
    {HeaderSetting::NaviHints, "NaviQuestHintFileId"},
    {HeaderSetting::KeepObject, "ObjectId"},
    {HeaderSetting::RoomType, "RoomType"},
    {HeaderSetting::Environment, "RoomEnvironmentType"},
    {HeaderSetting::LensMode, "LensMode"},
}};

[[nodiscard]] std::vector<NamedValue> listCameraTypes(const cparser::MacroTable& macros) {
    std::vector<NamedValue> values;
    for (const std::string_view name : macros.getDefinedNames()) {
        const cparser::Macro* macro =
            name.starts_with(CameraTypePrefix) ? macros.find(name) : nullptr;
        if (macro == nullptr || macro->functionLike) {
            continue;
        }
        std::vector<base::Diagnostic> ignored;
        const std::expected<cparser::Value, base::Diagnostic> value = cparser::evaluate(
            macro->body,
            cparser::EvalContext{.macros = &macros, .dialect = &z64::getDecompDialect()}, ignored);
        if (value) {
            values.push_back({.name = std::string(name), .value = cparser::getInteger(*value)});
        }
    }
    std::ranges::sort(values, {}, &NamedValue::value);
    return values;
}

void setLabels(std::vector<NamedValue>& values) {
    std::vector<std::string_view> names;
    names.reserve(values.size());
    for (const NamedValue& value : values) {
        names.emplace_back(value.name);
    }
    std::vector<std::string> labels = core::makeConstantLabels(names);
    for (usize i = 0; i < values.size(); ++i) {
        values[i].label = std::move(labels[i]);
    }
}

} // namespace

std::span<const NamedValue> HeaderNames::findValues(const HeaderSetting setting) const {
    const auto found = values.find(setting);
    return found != values.end() ? std::span<const NamedValue>(found->second)
                                 : std::span<const NamedValue>{};
}

HeaderNames readHeaderNames(const cparser::TypeTable& types, const cparser::MacroTable& macros) {
    HeaderNames names;
    for (const auto& [setting, enumeration] : Enumerations) {
        const cparser::Enumeration* found = types.findEnumeration(enumeration);
        if (found == nullptr) {
            continue;
        }
        std::vector<NamedValue>& values = names.values[setting];
        for (const cparser::Enumerator& constant : found->constants) {
            if (!constant.name.ends_with("_MAX")) {
                values.push_back({.name = constant.name, .value = constant.value});
            }
        }
        setLabels(values);
    }
    if (std::vector<NamedValue> cameraTypes = listCameraTypes(macros); !cameraTypes.empty()) {
        setLabels(cameraTypes);
        names.values[HeaderSetting::CameraType] = std::move(cameraTypes);
    }
    return names;
}

} // namespace toki::scene
