#include "MaterialBindings.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/SceneMaterials.h"

#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace toki::scene {

std::string makeMaterialKey(const SceneMaterial& material) {
    return material.isListed() ? material.name : std::format("{:016X}", material.content);
}

std::optional<MaterialBinding> findMaterialBinding(const MaterialBindings& bindings,
                                                   const SceneMaterial& material) {
    const auto found = bindings.materials.find(makeMaterialKey(material));
    return found != bindings.materials.end() ? std::optional(found->second) : std::nullopt;
}

MaterialBinding makeMaterialBinding(const Collision& collision,
                                    const std::optional<CollisionMaterial>& material) {
    if (!material || material->type >= collision.surfaceTypes.size()) {
        return MaterialBinding{};
    }
    return MaterialBinding{.isSolid = true,
                           .surface = collision.surfaceTypes[material->type],
                           .flags = material->flags};
}

std::expected<std::optional<CollisionMaterial>, std::string>
findBoundCollision(const Collision& collision, const MaterialBinding& binding) {
    if (!binding.isSolid) {
        return std::nullopt;
    }
    for (usize type = 0; type < collision.surfaceTypes.size(); ++type) {
        if (collision.surfaceTypes[type] == binding.surface) {
            return CollisionMaterial{.type = static_cast<u16>(type), .flags = binding.flags};
        }
    }
    return std::unexpected(std::format("the collision has no surface type {:08X} {:08X}",
                                       binding.surface.data[0], binding.surface.data[1]));
}

MaterialBindings makeReboundSurface(MaterialBindings bindings, const SurfaceType& from,
                                    const SurfaceType& to, const std::optional<u8> flags) {
    for (auto& [key, binding] : bindings.materials) {
        if (binding.isSolid && binding.surface == from && (!flags || binding.flags == *flags)) {
            binding.surface = to;
        }
    }
    return bindings;
}

} // namespace toki::scene
