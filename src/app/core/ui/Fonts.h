#pragma once

#include <imgui.h>

#include <filesystem>

namespace toki::core {

class Fonts {
public:
    static constexpr float BodySize = 15.0F;
    static constexpr float MonoSize = 13.0F;

    [[nodiscard]] static Fonts load(const std::filesystem::path& assetDir);

    [[nodiscard]] ImFont* getBody() const {
        return body;
    }

    [[nodiscard]] ImFont* getStrong() const {
        return strong;
    }

    [[nodiscard]] ImFont* getMono() const {
        return mono;
    }

private:
    ImFont* body = nullptr;
    ImFont* strong = nullptr;
    ImFont* mono = nullptr;
};

} // namespace toki::core
