#include "LightSettingEdits.h"

#include "app/scene/Types.h"
#include "app/scene/domain/ListEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"

#include <array>
#include <expected>
#include <vector>

namespace toki::scene {

namespace {

constexpr usize LightSettingSize = 0x16;
constexpr u16 FogNearMask = 0x03FF;
constexpr u16 BlendRateMask = 0x3F;
constexpr u16 BlendRateShift = 10;

constexpr ListShape LightSettingsShape{.code = CommandCode::LightSettingsList,
                                       .entrySize = LightSettingSize,
                                       .type = "EnvLightSettings",
                                       .noun = "light settings list",
                                       .suffix = "lightSettings"};

void writeTriple(std::vector<u8>& bytes, const usize at, const std::array<u8, 3>& values) {
    for (usize i = 0; i < values.size(); ++i) {
        bytes[at + i] = values[i];
    }
}

void writeTriple(std::vector<u8>& bytes, const usize at, const std::array<s8, 3>& values) {
    for (usize i = 0; i < values.size(); ++i) {
        bytes[at + i] = static_cast<u8>(values[i]);
    }
}

void writeU16(std::vector<u8>& bytes, const usize at, const u16 value) {
    bytes[at] = static_cast<u8>(value >> 8);
    bytes[at + 1] = static_cast<u8>(value);
}

void writeLightSetting(std::vector<u8>& bytes, const usize at, const LightSetting& setting) {
    writeTriple(bytes, at + 0x00, setting.ambientColor);
    writeTriple(bytes, at + 0x03, setting.light1Direction);
    writeTriple(bytes, at + 0x06, setting.light1Color);
    writeTriple(bytes, at + 0x09, setting.light2Direction);
    writeTriple(bytes, at + 0x0C, setting.light2Color);
    writeTriple(bytes, at + 0x0F, setting.fogColor);
    writeU16(bytes, at + 0x12,
             static_cast<u16>(((setting.blendRate & BlendRateMask) << BlendRateShift) |
                              (static_cast<u16>(setting.fogNear) & FogNearMask)));
    writeU16(bytes, at + 0x14, static_cast<u16>(setting.zFar));
}

[[nodiscard]] ListEditor makeEditor(SceneModel& model, const usize layer) {
    return {model, LightSettingsShape, layer, 0};
}

} // namespace

std::expected<void, base::Diagnostic> setLightSetting(SceneModel& model, const usize layer,
                                                      const usize index,
                                                      const LightSetting& setting) {
    return makeEditor(model, layer)
        .setEntry(index, [&setting](std::vector<u8>& bytes, const usize at) {
            writeLightSetting(bytes, at, setting);
        });
}

std::expected<usize, base::Diagnostic> addLightSetting(SceneModel& model, const usize layer,
                                                       const LightSetting& setting) {
    const std::expected<usize, base::Diagnostic> index = makeEditor(model, layer).addEntry();
    if (!index) {
        return index;
    }
    if (std::expected<void, base::Diagnostic> set = setLightSetting(model, layer, *index, setting);
        !set) {
        return std::unexpected(set.error());
    }
    return index;
}

std::expected<void, base::Diagnostic> removeLightSetting(SceneModel& model, const usize layer,
                                                         const usize index) {
    const ListEditor editor = makeEditor(model, layer);
    if (const std::expected<ListSite, base::Diagnostic> found = editor.find();
        found && found->count == 1 && index == 0) {
        return std::unexpected(base::makeError(
            {}, "the last light setting is not removed: the game lights every scene by one"));
    }
    return editor.removeEntry(index);
}

} // namespace toki::scene
