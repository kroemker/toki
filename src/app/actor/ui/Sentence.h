#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace toki::actor {

/// A line of words and tokens that wraps where the panel ends, as the
/// variant editor writes each variant, part and child: words are plain text,
/// and a token is a value that opens a popup to change it when clicked.
class Sentence {
public:
    Sentence() = default;

    void addWords(std::string_view words);

    /// A token showing `text`, whose popup draws `edit` and returns whether it
    /// changed what the token shows.
    template <typename Edit>
    [[nodiscard]] bool addToken(const char* id, const std::string& text, Edit&& edit) {
        placeNext(ImGui::CalcTextSize(text.c_str()).x + (ImGui::GetStyle().FramePadding.x * 2.0F));
        const bool isOpened = drawToken(id, text);
        bool isChanged = false;
        if (isOpened && ImGui::BeginPopup(id)) {
            isChanged = edit();
            ImGui::EndPopup();
        }
        return isChanged;
    }

    /// A small button among the words, as for the row's own actions.
    [[nodiscard]] bool addButton(const char* label, const char* tooltip);

private:
    void placeNext(float width);
    [[nodiscard]] static bool drawToken(const char* id, const std::string& text);

    bool isFirst = true;
};

} // namespace toki::actor
