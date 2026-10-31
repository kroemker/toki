#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/HeaderNames.h"
#include "app/scene/domain/HeaderSettings.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneSelection.h"
#include "app/workspace/domain/WorkspaceService.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hackeroot {
struct Tables;
} // namespace hackeroot

namespace toki::scene {

struct Scene;

/// The Properties panel for the scene or a room selected: the settings its
/// header holds in its commands, each a row, and a room's object list. Every
/// change is one edit, made once the frame's rows are drawn.
class HeaderForm {
public:
    /// Makes an edit of the scene's model as one step; false when it failed.
    using Apply = std::function<bool(Scene&, const SceneEditor::ModelEdit&)>;

    HeaderForm(const workspace::WorkspaceService& workspaceService, Apply apply)
        : workspaceService(workspaceService), apply(std::move(apply)) {}

    /// `selected` is how many nodes are selected, of which this is the last.
    void drawScene(Scene& scene, const SceneRef& where, usize selected);
    void drawRoom(Scene& scene, const RoomRef& where, usize selected);

    /// How a setting is edited.
    enum class SettingKind : u8 {
        Number,
        Toggle,
        /// One of the values the checkout names, or a number where it names
        /// none.
        Named,
    };

    struct SettingRow {
        HeaderSetting setting;
        const char* label;
        const char* help;
        SettingKind kind;
    };

    /// The rows of one command, the first row's setting naming it.
    struct SettingSection {
        const char* title;
        std::span<const SettingRow> rows;
    };

private:
    /// A setting being typed, kept while its field is held.
    struct HeldValue {
        HeaderSetting setting;
        s64 value = 0;
    };

    void drawSection(const SettingSection& section, std::span<const HeaderCommand> commands,
                     const HeaderNames& names, const HeaderRef& where);
    void drawSetting(const SettingRow& row, s64 value, const HeaderNames& names,
                     const HeaderRef& where);
    void drawNumber(const SettingRow& row, s64 value, const HeaderRef& where);
    void drawNamed(const SettingRow& row, s64 value, std::span<const NamedValue> values,
                   const HeaderRef& where);
    void drawObjects(const RoomHeader& header, const HeaderRef& where);
    void drawObjectRow(const std::vector<s16>& objects, usize index,
                       const hackeroot::Tables* tables, const HeaderRef& where);
    void drawObjectPicker(const std::vector<s16>& objects, const hackeroot::Tables& tables,
                          const HeaderRef& where);
    void setSetting(const HeaderRef& where, HeaderSetting setting, s64 value);
    void setObjects(const HeaderRef& where, std::vector<s16> objects);
    void applyPending(Scene& scene);

    const workspace::WorkspaceService& workspaceService;
    Apply apply;
    std::optional<HeldValue> held;
    std::string filter;
    SceneEditor::ModelEdit pending;
};

} // namespace toki::scene
