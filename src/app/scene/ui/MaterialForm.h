#pragma once

#include "app/scene/Types.h"
#include "app/scene/domain/MaterialBindings.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::scene {

struct SceneMaterial;

/// One choice of what a render material's faces are bound to.
struct BindingChoice {
    std::string label;
    /// What the material is bound to by it, or nothing to unbind it.
    std::optional<MaterialBinding> binding;
    /// How many of the material's faces binding it so changes, when their
    /// collision differs.
    usize changed = 0;
};

/// A render material's binding to the collision, as its settings show it.
struct BindingRows {
    std::vector<BindingChoice> choices;
    /// The choice the material is bound by.
    usize current = 0;
    /// Why the binding holds for none of the faces, if it does not.
    std::string problem;
    /// How many of the faces are not as the material is bound.
    usize unbound = 0;
};

/// The Properties panel's settings of a material the rooms are drawn with:
/// what it draws, and what its faces are made of while it is bound.
class MaterialForm {
public:
    /// Returns the binding picked, nothing inside to unbind, if one was.
    /// `binding` is nothing when the layer has no collision.
    [[nodiscard]] static std::optional<std::optional<MaterialBinding>>
    draw(const SceneMaterial& material, const std::optional<BindingRows>& binding);

    /// Says that the picked faces are drawn under several materials.
    static void drawMixed();
};

} // namespace toki::scene
