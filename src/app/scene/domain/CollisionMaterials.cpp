#include "CollisionMaterials.h"

#include "app/scene/Types.h"
#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/CollisionNames.h"
#include "app/scene/domain/MaterialBindings.h"
#include "app/scene/domain/SceneModel.h"
#include "base/Diagnostic.h"

#include <algorithm>
#include <array>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace toki::scene {

namespace {

constexpr std::array<PolygonFlag, 4> Flags{PolygonFlag::IgnoreCamera, PolygonFlag::IgnoreEntity,
                                           PolygonFlag::IgnoreProjectiles, PolygonFlag::Conveyor};

[[nodiscard]] const char* getFlagName(const PolygonFlag flag) {
    switch (flag) {
    case PolygonFlag::IgnoreCamera:
        return "Ignored by Camera";
    case PolygonFlag::IgnoreEntity:
        return "Ignored by Entities";
    case PolygonFlag::IgnoreProjectiles:
        return "Ignored by Projectiles";
    case PolygonFlag::Conveyor:
        return "Conveyor";
    }
    return "";
}

[[nodiscard]] u8 getFlagBit(const PolygonFlag flag) {
    return static_cast<u8>(1U << static_cast<u8>(flag));
}

} // namespace

CollisionMaterial getPolygonMaterial(const CollisionPolygon& polygon) {
    CollisionMaterial material{.type = polygon.type};
    for (const PolygonFlag flag : Flags) {
        material = setMaterialFlag(material, flag, hasPolygonFlag(polygon, flag));
    }
    return material;
}

bool hasMaterialFlag(const CollisionMaterial& material, const PolygonFlag flag) {
    return (material.flags & getFlagBit(flag)) != 0;
}

CollisionMaterial setMaterialFlag(CollisionMaterial material, const PolygonFlag flag,
                                  const bool isSet) {
    material.flags = static_cast<u8>(isSet ? material.flags | getFlagBit(flag)
                                           : material.flags & ~getFlagBit(flag));
    return material;
}

std::vector<SceneCollisionMaterial> listCollisionMaterials(const Collision& collision) {
    std::map<CollisionMaterial, std::vector<usize>> grouped;
    for (usize type = 0; type < collision.surfaceTypes.size(); ++type) {
        grouped[{.type = static_cast<u16>(type)}];
    }
    for (usize polygon = 0; polygon < collision.polygons.size(); ++polygon) {
        grouped[getPolygonMaterial(collision.polygons[polygon])].push_back(polygon);
    }
    std::vector<SceneCollisionMaterial> listed;
    for (auto& [material, polygons] : grouped) {
        const bool hasOthers = std::ranges::any_of(grouped, [&](const auto& other) {
            return other.first.type == material.type && other.first != material &&
                   !other.second.empty();
        });
        if (polygons.empty() && material.flags == 0 && hasOthers) {
            continue;
        }
        listed.push_back({.material = material, .polygons = std::move(polygons)});
    }
    return listed;
}

std::string makeCollisionMaterialName(const CollisionNames* names, const Collision& collision,
                                      const CollisionMaterial& material) {
    std::string name = makeSurfaceTypeLabel(names, collision, material.type);
    for (const PolygonFlag flag : Flags) {
        if (hasMaterialFlag(material, flag)) {
            name += " · ";
            name += getFlagName(flag);
        }
    }
    return name;
}

std::expected<void, base::Diagnostic> setPolygonMaterial(SceneModel& model, const usize layer,
                                                         const std::span<const usize> polygons,
                                                         const CollisionMaterial& material) {
    if (polygons.empty()) {
        return {};
    }
    if (std::expected<void, base::Diagnostic> set =
            setPolygonSurfaceType(model, layer, polygons, material.type);
        !set) {
        return set;
    }
    for (const PolygonFlag flag : Flags) {
        if (std::expected<void, base::Diagnostic> set =
                setPolygonFlag(model, layer, polygons, flag, hasMaterialFlag(material, flag));
            !set) {
            return set;
        }
    }
    return {};
}

std::expected<CollisionMaterial, base::Diagnostic>
setMaterialSurface(SceneModel& model, const usize layer, const Collision& collision,
                   const CollisionMaterial& material, const SurfaceType& surface) {
    std::vector<usize> own;
    bool isShared = false;
    for (usize polygon = 0; polygon < collision.polygons.size(); ++polygon) {
        const CollisionMaterial made = getPolygonMaterial(collision.polygons[polygon]);
        if (made == material) {
            own.push_back(polygon);
        } else if (made.type == material.type) {
            isShared = true;
        }
    }
    const auto rebind = [&model, &collision, &material, &surface](const std::optional<u8> flags) {
        if (material.type < collision.surfaceTypes.size()) {
            model.setMaterialBindings(makeReboundSurface(model.getMaterialBindings(),
                                                         collision.surfaceTypes[material.type],
                                                         surface, flags));
        }
    };
    if (!isShared || own.empty()) {
        if (std::expected<void, base::Diagnostic> set =
                setSurfaceType(model, layer, material.type, surface);
            !set) {
            return std::unexpected(set.error());
        }
        rebind(std::nullopt);
        return material;
    }
    const std::expected<usize, base::Diagnostic> added = addSurfaceType(model, layer, surface);
    if (!added) {
        return std::unexpected(added.error());
    }
    const CollisionMaterial moved{.type = static_cast<u16>(*added), .flags = material.flags};
    if (std::expected<void, base::Diagnostic> set =
            setPolygonSurfaceType(model, layer, own, moved.type);
        !set) {
        return std::unexpected(set.error());
    }
    rebind(material.flags);
    return moved;
}

std::expected<CollisionMaterial, base::Diagnostic>
setMaterialFlags(SceneModel& model, const usize layer, const Collision& collision,
                 const CollisionMaterial& material, const u8 flags) {
    std::vector<usize> own;
    for (usize polygon = 0; polygon < collision.polygons.size(); ++polygon) {
        if (getPolygonMaterial(collision.polygons[polygon]) == material) {
            own.push_back(polygon);
        }
    }
    const CollisionMaterial made{.type = material.type, .flags = flags};
    if (std::expected<void, base::Diagnostic> set = setPolygonMaterial(model, layer, own, made);
        !set) {
        return std::unexpected(set.error());
    }
    if (material.type < collision.surfaceTypes.size()) {
        model.setMaterialBindings(makeReboundFlags(model.getMaterialBindings(),
                                                   collision.surfaceTypes[material.type],
                                                   material.flags, flags));
    }
    return made;
}

} // namespace toki::scene
