#include "BuildPanel.h"

#include "app/build/Types.h"
#include "app/build/domain/AnsiText.h"
#include "app/build/domain/BuildRunner.h"
#include "app/core/ui/Fonts.h"
#include "app/core/ui/Icons.h"
#include "app/core/ui/Panel.h"
#include "app/core/ui/Theme.h"
#include "hackeroot/BuildCommand.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace toki::build {

namespace {

constexpr f32 BoldLift = 0.35F;

ImVec4 makeColor(const Rgb& rgb) {
    return {static_cast<f32>(rgb.red) / 255.0F, static_cast<f32>(rgb.green) / 255.0F,
            static_cast<f32>(rgb.blue) / 255.0F, 1.0F};
}

ImVec4 brighten(const ImVec4& color) {
    const auto lift = [](const f32 channel) { return channel + ((1.0F - channel) * BoldLift); };
    return {lift(color.x), lift(color.y), lift(color.z), color.w};
}

struct StatusStyle {
    const char* label = "";
    ImVec4 color;
};

StatusStyle findStatusStyle(const BuildStatus status) {
    switch (status) {
    case BuildStatus::Running:
        return {"Running…", core::palette::Accent};
    case BuildStatus::Succeeded:
        return {"Succeeded", core::palette::Success};
    case BuildStatus::Failed:
        return {"Failed", core::palette::Danger};
    case BuildStatus::Stopped:
        return {"Stopped", core::palette::Warning};
    case BuildStatus::Idle:
        break;
    }
    return {"No build yet", core::palette::TextMuted};
}

void drawLine(const std::string& line) {
    const std::vector<AnsiSpan> spans = parseAnsiLine(line);
    if (spans.empty()) {
        ImGui::NewLine();
        return;
    }
    for (usize i = 0; i < spans.size(); ++i) {
        const AnsiSpan& span = spans[i];
        ImVec4 color =
            span.style.foreground ? makeColor(*span.style.foreground) : core::palette::Text;
        if (span.style.bold) {
            color = brighten(color);
        }
        if (i > 0) {
            ImGui::SameLine(0.0F, 0.0F);
        }
        if (span.style.background) {
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const ImVec2 size = ImGui::CalcTextSize(span.text.c_str());
            ImGui::GetWindowDrawList()->AddRectFilled(
                start, ImVec2(start.x + size.x, start.y + size.y),
                ImGui::ColorConvertFloat4ToU32(makeColor(*span.style.background)));
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(span.text.c_str());
        ImGui::PopStyleColor();
    }
}

} // namespace

BuildPanel::BuildPanel(BuildRunner& buildRunner, const core::Fonts& fonts,
                       std::function<void(hackeroot::BuildTarget)> startBuild)
    : Panel("build", "Build", core::icons::Hammer, core::DockSlot::Bottom),
      buildRunner(buildRunner), fonts(fonts), startBuild(std::move(startBuild)) {}

void BuildPanel::draw() {
    drawToolbar();
    ImGui::Separator();
    drawLines();
}

void BuildPanel::drawToolbar() {
    const bool running = buildRunner.isRunning();
    ImGui::BeginDisabled(running);
    if (ImGui::Button((std::string(core::icons::Hammer) + " Build").c_str())) {
        startBuild(hackeroot::BuildTarget::Build);
    }
    ImGui::SetItemTooltip("Build the checkout (Ctrl+B)");
    ImGui::SameLine();
    if (ImGui::Button((std::string(core::icons::Play) + " Run").c_str())) {
        startBuild(hackeroot::BuildTarget::Run);
    }
    ImGui::SetItemTooltip("Build, then run it (F5)");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button((std::string(core::icons::Stop) + " Stop").c_str())) {
        buildRunner.stop();
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0.0F, core::space::Huge);
    const StatusStyle status = findStatusStyle(buildRunner.getStatus());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(status.color, "%s", status.label);

    ImGui::SameLine(0.0F, core::space::Huge);
    if (ImGui::Button(core::icons::Copy)) {
        copyLines();
    }
    ImGui::SetItemTooltip("Copy the output");
    ImGui::SameLine();
    if (ImGui::Button(core::icons::Broom)) {
        buildRunner.getLog().clear();
    }
    ImGui::SetItemTooltip("Clear the output");
}

void BuildPanel::drawLines() {
    if (!ImGui::BeginChild("##output", ImVec2(0.0F, 0.0F), ImGuiChildFlags_None,
                           ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    const auto& lines = buildRunner.getLog().getLines();
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
    ImGui::PushFont(fonts.getMono(), core::Fonts::MonoSize);
    if (lines.empty()) {
        ImGui::TextColored(core::palette::TextMuted, "No output yet.");
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lines.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            drawLine(lines[static_cast<usize>(row)]);
        }
    }
    ImGui::PopFont();

    if (atBottom) {
        ImGui::SetScrollHereY(1.0F);
    }
    ImGui::EndChild();
}

void BuildPanel::copyLines() const {
    std::string text;
    for (const std::string& line : buildRunner.getLog().getLines()) {
        for (const AnsiSpan& span : parseAnsiLine(line)) {
            text += span.text;
        }
        text += '\n';
    }
    ImGui::SetClipboardText(text.c_str());
}

} // namespace toki::build
