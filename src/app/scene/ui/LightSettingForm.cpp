#include "LightSettingForm.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/scene/domain/LightSettingEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneModel.h"
#include "app/scene/domain/SceneView.h"

#include <imgui.h>

#include <array>
#include <cfloat>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

constexpr float LabelWidth = 8.0F;
constexpr float ColorScale = 255.0F;
constexpr s16 FogNearMaximum = 0x03FF;
constexpr u8 BlendRateMaximum = 0x3F;
constexpr s16 ZFarMaximum = 0x7FFF;
/// What a light setting added to a layer without one starts as: a plain
/// white light from above, and fog that starts far out.
constexpr LightSetting DefaultSetting{.ambientColor = {0x50, 0x50, 0x50},
                                      .light1Direction = {0x49, 0x49, 0x49},
                                      .light1Color = {0xB4, 0xB4, 0xB4},
                                      .light2Direction = {-0x49, -0x49, -0x49},
                                      .light2Color = {0x28, 0x28, 0x28},
                                      .fogColor = {0x80, 0x80, 0x80},
                                      .fogNear = 0x03E0,
                                      .blendRate = 1,
                                      .zFar = 0x3200};

void drawColor(const char* label, const char* help, std::array<u8, 3>& color) {
    core::drawPropertyLabel(label, help);
    std::array<float, 3> channels{color[0] / ColorScale, color[1] / ColorScale,
                                  color[2] / ColorScale};
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(label);
    if (ImGui::ColorEdit3("##color", channels.data(), ImGuiColorEditFlags_DisplayRGB)) {
        for (usize i = 0; i < color.size(); ++i) {
            color[i] = static_cast<u8>(channels[i] * ColorScale + 0.5F);
        }
    }
    ImGui::PopID();
}

void drawDirection(const char* label, const char* help, std::array<s8, 3>& direction) {
    core::drawPropertyLabel(label, help);
    constexpr s8 Minimum = -128;
    constexpr s8 Maximum = 127;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(label);
    ImGui::DragScalarN("##direction", ImGuiDataType_S8, direction.data(), 3, 0.5F, &Minimum,
                       &Maximum);
    ImGui::PopID();
}

void drawRows(LightSetting& setting) {
    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen) &&
        core::beginProperties("##lights", LabelWidth)) {
        drawColor("Ambient Color", "The colour every surface is lit by, whichever way it faces.",
                  setting.ambientColor);
        drawDirection("Light 1 Direction",
                      "The direction the first light shines from, a signed byte along each axis.",
                      setting.light1Direction);
        drawColor("Light 1 Color", "The colour of the first light.", setting.light1Color);
        drawDirection("Light 2 Direction", "The direction the second light shines from.",
                      setting.light2Direction);
        drawColor("Light 2 Color", "The colour of the second light.", setting.light2Color);
        core::endProperties();
    }
    if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen) &&
        core::beginProperties("##fog", LabelWidth)) {
        drawColor("Fog Color", "The colour the scene fades to with distance.", setting.fogColor);
        core::drawPropertyLabel("Fog Near", "Where the fog begins, between the camera at 0 and "
                                            "the far plane at 1000.");
        constexpr s16 Zero = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##fogNear", ImGuiDataType_S16, &setting.fogNear, 1.0F, &Zero,
                          &FogNearMaximum);
        core::drawPropertyLabel("Blend Rate", "How fast the game blends into this setting when "
                                              "it changes to it, in steps of four.");
        constexpr u8 NoRate = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##blendRate", ImGuiDataType_U8, &setting.blendRate, 0.2F, &NoRate,
                          &BlendRateMaximum);
        core::drawPropertyLabel("Z Far", "How far the camera sees: the far plane, where the fog "
                                         "is thickest.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##zFar", ImGuiDataType_S16, &setting.zFar, 10.0F, &Zero, &ZFarMaximum);
        core::endProperties();
    }
}

} // namespace

void LightSettingForm::draw(const Scene& scene, const LightSettingRef& where,
                            const usize selected) {
    const std::optional<LightSetting> stored = findLightSetting(scene, where.layer, where.index);
    if (!stored) {
        return;
    }
    if (!draft || draft->where != where) {
        draft = Draft{.where = where, .setting = *stored};
    }
    const SceneLayer& layer = scene.layers[where.layer];
    ImGui::TextColored(core::palette::Warning, "%s", core::icons::Sun);
    ImGui::SameLine();
    ImGui::TextUnformatted(makeLightSettingName(layer.header, where.index).c_str());
    ImGui::TextColored(
        core::palette::TextMuted, "%s",
        std::format("Light setting {} · {}", where.index, makeLayerName(layer.index)).c_str());
    if (selected > 1) {
        ImGui::TextColored(core::palette::Accent, "%zu selected; shown is the one picked last.",
                           selected);
    }
    ImGui::Spacing();
    drawRows(draft->setting);
    if (draft->setting != *stored && !ImGui::IsAnyItemActive()) {
        const usize gameLayer = layer.index;
        pick(
            [gameLayer, edited = *draft](SceneModel& model) {
                return setLightSetting(model, gameLayer, edited.where.index, edited.setting);
            },
            std::nullopt);
    }
    ImGui::Spacing();
    if (ImGui::Button("Duplicate")) {
        pickAdd(scene, where.layer, draft->setting);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(layer.header.lightSettings.size() <= 1);
    ImGui::PushStyleColor(ImGuiCol_Text, core::palette::Danger);
    if (ImGui::Button("Delete")) {
        pickRemove(scene, where);
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", layer.header.lightSettings.size() <= 1
                                    ? "The game lights every scene by one light setting."
                                    : "The settings after it move down one: where the time of "
                                      "day picks the setting, each takes the time before it.");
}

void LightSettingForm::drawMenu(const Scene& scene, const LightSettingRef& where) {
    const std::optional<LightSetting> setting = findLightSetting(scene, where.layer, where.index);
    if (!setting) {
        return;
    }
    if (ImGui::MenuItem("Duplicate")) {
        pickAdd(scene, where.layer, *setting);
    }
    if (ImGui::MenuItem("Delete", nullptr, false,
                        scene.layers[where.layer].header.lightSettings.size() > 1)) {
        pickRemove(scene, where);
    }
}

void LightSettingForm::drawGroupMenu(const Scene& scene, const usize layer) {
    if (layer >= scene.layers.size()) {
        return;
    }
    if (ImGui::MenuItem("Add Light Setting")) {
        const std::vector<LightSetting>& settings = scene.layers[layer].header.lightSettings;
        pickAdd(scene, layer, settings.empty() ? DefaultSetting : settings.back());
    }
}

std::optional<LightSettingForm::Draft>
LightSettingForm::findChangedDraft(const Scene& scene) const {
    if (!draft || findLightSetting(scene, draft->where.layer, draft->where.index) ==
                      std::optional(draft->setting)) {
        return std::nullopt;
    }
    return draft;
}

void LightSettingForm::pickAdd(const Scene& scene, const usize layer, const LightSetting& setting) {
    const usize gameLayer = scene.layers[layer].index;
    pick(
        [gameLayer, setting](SceneModel& model) {
            return addLightSetting(model, gameLayer, setting).transform([](usize) {});
        },
        LightSettingRef{.layer = layer, .index = scene.layers[layer].header.lightSettings.size()});
}

void LightSettingForm::pickRemove(const Scene& scene, const LightSettingRef& where) {
    const usize gameLayer = scene.layers[where.layer].index;
    pick([gameLayer, index = where.index](
             SceneModel& model) { return removeLightSetting(model, gameLayer, index); },
         std::nullopt);
}

} // namespace toki::scene
