#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/scene/domain/SceneSelection.h"

#include <functional>
#include <optional>
#include <utility>

namespace toki::scene {

struct Scene;

/// The Properties panel for a light setting selected, and the menus that add,
/// copy and remove light settings. A setting is edited as a draft the
/// viewport shows, and made once nothing of the form is held.
class LightSettingForm {
public:
    /// Makes `edit` as one step once the panels are drawn, and selects
    /// `select` when it was made.
    using Pick = std::function<void(SceneEditor::ModelEdit edit, std::optional<SceneNode> select)>;

    explicit LightSettingForm(Pick pick) : pick(std::move(pick)) {}

    /// A light setting as the form holds it while it is edited.
    struct Draft {
        LightSettingRef where;
        LightSetting setting;

        friend bool operator==(const Draft&, const Draft&) = default;
    };

    /// `selected` is how many nodes are selected, of which this is the last.
    void draw(const Scene& scene, const LightSettingRef& where, usize selected);

    /// The items of a light setting's context menu.
    void drawMenu(const Scene& scene, const LightSettingRef& where);

    /// The items of the light settings' group's context menu, for `layer`, an
    /// index into `Scene::layers`.
    void drawGroupMenu(const Scene& scene, usize layer);

    /// The setting being edited, as it is now, while that differs from what
    /// `scene` holds; nothing otherwise.
    [[nodiscard]] std::optional<Draft> findChangedDraft(const Scene& scene) const;

    /// Takes the setting as the scene holds it again, after an edit.
    void reset() {
        draft.reset();
    }

private:
    void pickAdd(const Scene& scene, usize layer, const LightSetting& setting);
    void pickRemove(const Scene& scene, const LightSettingRef& where);

    Pick pick;
    std::optional<Draft> draft;
};

} // namespace toki::scene
