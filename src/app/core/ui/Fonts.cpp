#include "Fonts.h"

#include "app/core/domain/Log.h"
#include "app/core/domain/Utf8.h"
#include "app/core/ui/Icons.h"

#include <array>
#include <filesystem>
#include <system_error>

namespace toki::core {

namespace {

constexpr const char* BodyFile = "Inter-Regular.ttf";
constexpr const char* StrongFile = "Inter-SemiBold.ttf";
constexpr const char* MonoFile = "JetBrainsMono-Regular.ttf";
constexpr const char* IconFile = "Phosphor.ttf";

constexpr std::array<ImWchar, 3> IconGlyphRange{icons::RangeMin, icons::RangeMax, 0};

[[nodiscard]] bool isFile(const std::filesystem::path& path) {
    std::error_code status;
    return std::filesystem::is_regular_file(path, status);
}

[[nodiscard]] ImFont* addFont(ImFontAtlas& atlas, const std::filesystem::path& path, float size) {
    if (!isFile(path)) {
        logError("font missing: {}", encodeUtf8(path));
        return nullptr;
    }
    ImFontConfig config;
    config.GlyphExcludeRanges = IconGlyphRange.data();
    return atlas.AddFontFromFileTTF(encodeUtf8(path).c_str(), size, &config);
}

void mergeIcons(ImFontAtlas& atlas, const std::filesystem::path& path, float size) {
    if (!isFile(path)) {
        logError("icon font missing: {} — icons will render as boxes", encodeUtf8(path));
        return;
    }

    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphMinAdvanceX = size;
    config.GlyphOffset = ImVec2(0.0F, 1.0F);
    atlas.AddFontFromFileTTF(encodeUtf8(path).c_str(), size, &config);
}

[[nodiscard]] ImFont* addTextFont(ImFontAtlas& atlas, const std::filesystem::path& dir,
                                  const char* file, float size) {
    ImFont* font = addFont(atlas, dir / file, size);
    if (font == nullptr) {
        return atlas.AddFontDefault();
    }
    mergeIcons(atlas, dir / IconFile, size);
    return font;
}

} // namespace

Fonts Fonts::load(const std::filesystem::path& assetDir) {
    const std::filesystem::path dir = assetDir / "fonts";
    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;

    Fonts fonts;
    fonts.body = addTextFont(atlas, dir, BodyFile, BodySize);
    fonts.strong = addTextFont(atlas, dir, StrongFile, BodySize);

    fonts.mono = addFont(atlas, dir / MonoFile, MonoSize);
    if (fonts.mono == nullptr) {
        fonts.mono = fonts.body;
    }

    ImGui::GetIO().FontDefault = fonts.body;
    return fonts;
}

} // namespace toki::core
