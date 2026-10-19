#pragma once

#include "app/core/domain/Settings.h"
#include "app/scene/Types.h"
#include "app/scene/domain/SceneHeader.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Tables.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace toki::scene {

/// The rows of a placement as the game stores it, drawn into the property
/// table open: its type, position, rotation and params, raw. Each returns
/// true once an edit is to be made, with the placement holding it.
class ActorForm {
public:
    explicit ActorForm(const workspace::WorkspaceService& workspaceService)
        : workspaceService(workspaceService) {}

    /// Shows rotations as `format`; `changed` is told when the form's own
    /// button switches it.
    void setAngleFormat(core::AngleFormat format, std::function<void(core::AngleFormat)> changed) {
        angleFormat = format;
        changeAngleFormat = std::move(changed);
    }

    /// What finds the name the actor database gives an actor, by its enum
    /// name, shown in place of the enum name where there is one.
    using FindName = std::function<std::optional<std::string>(std::string_view enumName)>;

    void setNameFinder(FindName finder) {
        findName = std::move(finder);
    }

    /// `isParamAxis` says which axes of the rotation the actor reads params
    /// from, which are shown raw whatever the format.
    [[nodiscard]] bool drawActor(ActorPlacement& placement, const std::array<bool, 3>& isParamAxis);

    [[nodiscard]] bool drawTransitionActor(TransitionActorPlacement& placement,
                                           bool isRotationParams);

private:
    [[nodiscard]] bool editType(s16& id);
    [[nodiscard]] std::string makeActorLabel(const hackeroot::ActorEntry& actor) const;
    [[nodiscard]] bool editRotation(std::span<s16> angles, std::span<const bool> isParamAxis);
    void drawAngleFormatButton();

    const workspace::WorkspaceService& workspaceService;
    std::string actorFilter;
    core::AngleFormat angleFormat = core::AngleFormat::Binary;
    std::function<void(core::AngleFormat)> changeAngleFormat;
    FindName findName;
};

} // namespace toki::scene
