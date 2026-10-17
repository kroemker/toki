#include "ViewportStatusBar.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/PropertyTable.h"
#include "app/core/ui/Theme.h"
#include "app/viewport/Types.h"
#include "app/viewport/domain/FrameStatistics.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "n64/Memory.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace toki::viewport {

namespace {

constexpr usize HistoryLength = 120;
constexpr usize LargestShown = 8;
constexpr float LabelWidth = 10.0F;
constexpr float BarWidth = 7.0F;
constexpr float BarHeight = 0.5F;
constexpr float PopoverWidth = 24.0F;
constexpr float PlotHeight = 4.0F;
constexpr f64 KiB = 1024.0;
constexpr f64 MiB = 1024.0 * 1024.0;
constexpr const char* GeometryPopup = "##geometry";
constexpr const char* MemoryPopup = "##memory";
constexpr const char* FramePopup = "##frame";

constexpr std::array<ImVec4, ImageKindCount> KindColors{
    core::palette::TextMuted,
    core::palette::Accent,
    core::palette::Success,
    core::palette::Warning,
    ImVec4(0.77F, 0.54F, 0.94F, 1.0F),
    ImVec4(0.55F, 0.58F, 0.64F, 1.0F),
};
constexpr ImVec4 FrameListColor{0.45F, 0.47F, 0.52F, 1.0F};

[[nodiscard]] std::string formatCount(const usize count) {
    std::string digits = std::to_string(count);
    for (usize at = digits.size(); at > 3; at -= 3) {
        digits.insert(at - 3, ",");
    }
    return digits;
}

[[nodiscard]] std::string formatBytes(const usize bytes) {
    const auto size = static_cast<f64>(bytes);
    return size >= MiB ? std::format("{:.1f} MiB", size / MiB)
                       : std::format("{:.0f} KiB", size / KiB);
}

[[nodiscard]] bool hasLeftOut(const FrameStatistics& statistics) {
    return std::ranges::any_of(statistics.images,
                               [](const ImageUse& image) { return !image.isMapped; });
}

void drawMemoryBar(ImDrawList& drawList, const ImVec2 min, const ImVec2 max,
                   const FrameStatistics& statistics) {
    drawList.AddRectFilled(min, max, ImGui::GetColorU32(core::palette::SurfaceActive));
    const float width = max.x - min.x;
    float x = min.x;
    const auto addSpan = [&](const usize bytes, const ImVec4& color) {
        const float span =
            width * static_cast<float>(bytes) / static_cast<float>(n64::AddressSpaceSize);
        drawList.AddRectFilled(ImVec2(x, min.y), ImVec2(std::min(x + span, max.x), max.y),
                               ImGui::GetColorU32(color));
        x += span;
    };
    addSpan(statistics.codeRegion, KindColors[static_cast<usize>(ImageKind::Code)]);
    for (const ImageUse& image : statistics.images) {
        if (image.isMapped) {
            addSpan(image.size, KindColors[static_cast<usize>(image.kind)]);
        }
    }
    addSpan(statistics.frameListSize, FrameListColor);
    if (hasLeftOut(statistics) && x < max.x) {
        drawList.AddRectFilled(ImVec2(x, min.y), max, ImGui::GetColorU32(core::palette::Danger));
    }
}

/// One clickable item of the line, `width` wide, drawn by `drawContent` from
/// its top left; opens `popup` above it, lined up with its left edge or, with
/// `alignsRight`, its right one. True while the popup is open.
[[nodiscard]] bool beginStatusItem(const char* popup, const float width, const bool alignsRight,
                                   const std::function<void(ImDrawList&, ImVec2)>& drawContent) {
    const ImVec2 padding{core::space::Medium, 0.0F};
    const ImVec2 size{width + (padding.x * 2.0F), ImGui::GetFrameHeight()};
    const ImVec2 min = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton(popup, size)) {
        ImGui::OpenPopup(popup);
    }
    const ImVec2 max{min.x + size.x, min.y + size.y};
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered() || ImGui::IsPopupOpen(popup)) {
        drawList.AddRectFilled(min, max, ImGui::GetColorU32(core::palette::SurfaceHovered),
                               core::space::Small);
    }
    drawContent(drawList, ImVec2(min.x + padding.x, min.y));
    ImGui::SetNextWindowPos(ImVec2(alignsRight ? max.x : min.x, min.y - core::space::Small),
                            ImGuiCond_Always, ImVec2(alignsRight ? 1.0F : 0.0F, 1.0F));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * PopoverWidth, 0.0F));
    return ImGui::BeginPopup(popup);
}

void drawText(ImDrawList& drawList, const ImVec2 at, const std::string& text, const ImVec4& color) {
    const float y = at.y + ((ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) / 2.0F);
    drawList.AddText(ImVec2(at.x, y), ImGui::GetColorU32(color), text.c_str());
}

void drawValueRow(const std::string& label, const std::string& value, const std::string& help) {
    core::drawPropertyLabel(label, help);
    ImGui::TextUnformatted(value.c_str());
}

void drawGeometry(const FrameReport& report) {
    if (!core::beginProperties("##geometryForm", LabelWidth)) {
        return;
    }
    const FrameStatistics& statistics = report.statistics;
    drawValueRow("Triangles Drawn", formatCount(statistics.rsp.triangles),
                 "Triangles the RSP sent to the RDP, after culling.");
    drawValueRow("Triangles Culled", formatCount(statistics.rsp.culled),
                 "Triangles the RSP culled as facing away, as F3DEX2 does.");
    drawValueRow("Vertices Loaded", formatCount(statistics.rsp.vertices),
                 "Vertices G_VTX loaded into the vertex buffer.");
    drawValueRow("Commands Run", formatCount(statistics.rsp.commands),
                 "Display list commands the RSP ran.");
    drawValueRow("Texture Loads", formatCount(statistics.textureLoads),
                 "G_LOADBLOCK, G_LOADTILE and G_LOADTLUT into texture memory.");
    drawValueRow("Draw Calls", formatCount(report.drawCalls),
                 "Draw calls on the GPU, after primitives under equal state were merged.");
    core::endProperties();
}

void drawMemory(const FrameStatistics& statistics) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max{min.x + ImGui::GetContentRegionAvail().x,
                     min.y + (ImGui::GetFontSize() * BarHeight * 1.5F)};
    drawMemoryBar(*ImGui::GetWindowDrawList(), min, max, statistics);
    ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));

    std::array<usize, ImageKindCount> kinds{};
    std::vector<ImageUse> mapped;
    std::vector<ImageUse> leftOut;
    for (const ImageUse& image : statistics.images) {
        if (image.isMapped) {
            kinds[static_cast<usize>(image.kind)] += image.size;
            mapped.push_back(image);
        } else {
            leftOut.push_back(image);
        }
    }
    kinds[static_cast<usize>(ImageKind::Code)] += statistics.codeRegion;
    if (core::beginProperties("##memoryForm", LabelWidth)) {
        for (usize kind = 0; kind < ImageKindCount; ++kind) {
            if (kinds[kind] == 0) {
                continue;
            }
            core::drawPropertyLabel(getImageKindName(static_cast<ImageKind>(kind)));
            ImGui::TextColored(KindColors[kind], "%s", formatBytes(kinds[kind]).c_str());
        }
        drawValueRow("Frame", formatBytes(statistics.frameListSize),
                     "The display list and matrices the viewport writes for the frame.");
        drawValueRow("Used", std::format("{} of 16 MiB", formatBytes(statistics.used)),
                     "From address 0 to the end of the frame's display list, of the 16 MiB "
                     "the RSP addresses.");
        core::endProperties();
    }
    if (!leftOut.empty()) {
        ImGui::SeparatorText("Left Out");
        if (core::beginProperties("##leftOutForm", LabelWidth)) {
            for (const ImageUse& image : leftOut) {
                core::drawPropertyLabel(image.name, "Does not fit, so every draw that needs it "
                                                    "is skipped.");
                ImGui::TextColored(core::palette::Danger, "%s", formatBytes(image.size).c_str());
            }
            core::endProperties();
        }
    }
    std::ranges::sort(mapped, std::ranges::greater{}, &ImageUse::size);
    mapped.resize(std::min(mapped.size(), LargestShown));
    ImGui::SeparatorText("Largest Images");
    if (core::beginProperties("##largestForm", LabelWidth)) {
        for (const ImageUse& image : mapped) {
            core::drawPropertyLabel(image.name, getImageKindName(image.kind));
            ImGui::TextUnformatted(formatBytes(image.size).c_str());
        }
        core::endProperties();
    }
}

void drawFrame(const FrameReport& report, const std::vector<float>& history) {
    const auto highest = std::ranges::max_element(history);
    const float top = highest != history.end() ? *highest * 1.25F : 1.0F;
    ImGui::PlotLines("##history", history.data(), static_cast<int>(history.size()), 0, nullptr,
                     0.0F, top,
                     ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * PlotHeight));
    if (!core::beginProperties("##frameForm", LabelWidth)) {
        return;
    }
    const FrameTimings& timings = report.statistics.timings;
    drawValueRow("Place", std::format("{:.2f} ms", timings.place),
                 "Mapping the images into memory and writing the frame's display list.");
    drawValueRow("RSP", std::format("{:.2f} ms", timings.rsp),
                 "F3DEX2 on the CPU: transform, lighting, clipping and culling.");
    drawValueRow("RDP", std::format("{:.2f} ms", timings.rdp),
                 "Texture memory, combiner and blender state for every primitive.");
    drawValueRow("Render", std::format("{:.2f} ms", report.render),
                 "Building draw calls, uploading their buffers and submitting them.");
    core::drawPropertyLabel("GPU", "SDL's GPU API has no timestamp queries, so the GPU's own "
                                   "time is not known.");
    ImGui::TextColored(core::palette::TextMuted, "Not measured");
    drawValueRow("App", std::format("{:.0f} FPS", ImGui::GetIO().Framerate),
                 "How often the whole window is drawn. The viewport renders only when "
                 "something it shows changed.");
    core::endProperties();
}

[[nodiscard]] f64 findCpuTime(const FrameReport& report) {
    const FrameTimings& timings = report.statistics.timings;
    return timings.place + timings.rsp + timings.rdp + report.render;
}

} // namespace

void ViewportStatusBar::addFrame(FrameReport report) {
    history.push_back(static_cast<float>(findCpuTime(report)));
    if (history.size() > HistoryLength) {
        history.erase(history.begin());
    }
    lastFrame = std::move(report);
}

float ViewportStatusBar::getHeight() {
    return ImGui::GetFrameHeight() + core::space::Small;
}

void ViewportStatusBar::draw() const {
    ImGui::SetCursorScreenPos(
        ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + core::space::Small));
    if (!lastFrame) {
        ImGui::TextColored(core::palette::TextMuted, "No frame drawn yet.");
        return;
    }
    const FrameReport& report = *lastFrame;
    const FrameStatistics& statistics = report.statistics;
    const ImVec4 text = core::palette::Text;
    const ImVec4 muted = core::palette::TextMuted;

    const std::string geometry =
        std::format("{} {} tris  ·  {} draws", core::icons::Triangle,
                    formatCount(statistics.rsp.triangles), formatCount(report.drawCalls));
    if (beginStatusItem(GeometryPopup, ImGui::CalcTextSize(geometry.c_str()).x, false,
                        [&](ImDrawList& drawList, const ImVec2 at) {
                            drawText(drawList, at, geometry, text);
                        })) {
        drawGeometry(report);
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0F, core::space::Small);
    const bool isOver = hasLeftOut(statistics);
    const auto leftOut = static_cast<usize>(std::ranges::count_if(
        statistics.images, [](const ImageUse& image) { return !image.isMapped; }));
    const std::string memory =
        isOver ? std::format("{} / 16 MiB  ·  {} left out", formatBytes(statistics.used), leftOut)
               : std::format("{} / 16 MiB", formatBytes(statistics.used));
    const float barWidth = ImGui::GetFontSize() * BarWidth;
    const float memoryWidth =
        barWidth + core::space::Medium + ImGui::CalcTextSize(memory.c_str()).x;
    if (beginStatusItem(
            MemoryPopup, memoryWidth, false, [&](ImDrawList& drawList, const ImVec2 at) {
                const float barTop =
                    at.y + ((ImGui::GetFrameHeight() - (ImGui::GetFontSize() * BarHeight)) / 2.0F);
                drawMemoryBar(drawList, ImVec2(at.x, barTop),
                              ImVec2(at.x + barWidth, barTop + (ImGui::GetFontSize() * BarHeight)),
                              statistics);
                drawText(drawList, ImVec2(at.x + barWidth + core::space::Medium, at.y), memory,
                         isOver ? core::palette::Danger : text);
            })) {
        drawMemory(statistics);
        ImGui::EndPopup();
    }

    const std::string cost = std::format("{} {:.1f} ms  ·  {:.0f} FPS", core::icons::Timer,
                                         findCpuTime(report), ImGui::GetIO().Framerate);
    const float costWidth = ImGui::CalcTextSize(cost.c_str()).x;
    ImGui::SameLine(0.0F, core::space::Small);
    const float costItemWidth = costWidth + (core::space::Medium * 2.0F);
    if (ImGui::GetContentRegionAvail().x > costItemWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                             costItemWidth);
    }
    if (beginStatusItem(FramePopup, costWidth, true, [&](ImDrawList& drawList, const ImVec2 at) {
            drawText(drawList, at, cost, muted);
        })) {
        drawFrame(report, history);
        ImGui::EndPopup();
    }
}

} // namespace toki::viewport
