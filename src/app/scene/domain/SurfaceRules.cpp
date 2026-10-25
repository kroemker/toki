#include "SurfaceRules.h"

#include "app/scene/domain/CollisionNames.h"

#include <algorithm>
#include <array>
#include <format>

namespace toki::scene {

namespace {

constexpr u8 BurningFloor = 2;
constexpr u8 BurningFasterFloor = 3;
constexpr u8 ExitStandingFloor = 10;
constexpr u8 HoleExitFloor = 11;
constexpr u8 ExitVoidOutEffect = 2;
constexpr const char* RequiresExit = "Ignored. Requires an exit.";
constexpr const char* RequiresFloor = "Ignored. Requires a floor polygon.";
constexpr const char* RequiresConveyorSpeed = "Ignored. Requires a conveyor speed.";

[[nodiscard]] bool hasPolygonOfKind(const Collision& collision, const usize type,
                                    const PolygonKind kind) {
    return std::ranges::any_of(collision.polygons, [&](const CollisionPolygon& polygon) {
        return polygon.type == type && findPolygonKind(polygon) == kind;
    });
}

[[nodiscard]] std::string makeFloorTypeName(const CollisionNames* names, const u8 value) {
    const SurfaceValueName* named =
        names != nullptr ? names->findName(SurfaceSetting::FloorType, value) : nullptr;
    return named != nullptr ? named->name : std::format("{}", value);
}

[[nodiscard]] IgnoredSettings listFloorSettings(const SurfaceFields& fields) {
    IgnoredSettings ignored;
    const std::array<std::pair<SurfaceField, bool>, 5> floorSettings{{
        {SurfaceField::Exit, fields.exitIndex != 0},
        {SurfaceField::FloorType, fields.floorType != 0},
        {SurfaceField::FloorProperty, fields.floorProperty != 0},
        {SurfaceField::FloorEffect, fields.floorEffect != 0},
        {SurfaceField::ConveyorSpeed, fields.conveyorSpeed != 0},
    }};
    for (const auto& [field, isSet] : floorSettings) {
        if (isSet) {
            ignored.emplace(field, RequiresFloor);
        }
    }
    return ignored;
}

} // namespace

IgnoredSettings listIgnoredSettings(const Collision& collision, const usize type,
                                    const CollisionNames* names) {
    IgnoredSettings ignored;
    ignored.emplace(SurfaceField::Unknown18, "Ignored. No code reads it.");
    if (type >= collision.surfaceTypes.size()) {
        return ignored;
    }
    const SurfaceFields fields = readSurfaceFields(collision.surfaceTypes[type]);
    if (!hasPolygonOfKind(collision, type, PolygonKind::Floor)) {
        ignored.merge(listFloorSettings(fields));
    }
    const bool isExitFloor =
        fields.floorType == ExitStandingFloor || fields.floorType == HoleExitFloor;
    if (fields.exitIndex == 0 && isExitFloor) {
        ignored.emplace(SurfaceField::FloorType, RequiresExit);
    }
    if (fields.exitIndex == 0 && fields.floorEffect == ExitVoidOutEffect) {
        ignored.emplace(SurfaceField::FloorEffect, RequiresExit);
    }
    if (fields.conveyorSpeed == 0 && fields.conveyorDirection != 0) {
        ignored.emplace(SurfaceField::ConveyorDirection, RequiresConveyorSpeed);
    }
    const bool isBurning =
        fields.floorType == BurningFloor || fields.floorType == BurningFasterFloor;
    if (fields.unk27 && !isBurning && !hasPolygonOfKind(collision, type, PolygonKind::Wall)) {
        ignored.emplace(SurfaceField::ContactDamage,
                        std::format("Ignored. Requires a wall polygon, or floor type {} or {}.",
                                    makeFloorTypeName(names, BurningFloor),
                                    makeFloorTypeName(names, BurningFasterFloor)));
    }
    return ignored;
}

std::optional<std::string> findIgnoredFlag(const Collision& collision,
                                           const CollisionPolygon& polygon,
                                           const PolygonFlag flag) {
    if (flag != PolygonFlag::Conveyor || !hasPolygonFlag(polygon, flag)) {
        return std::nullopt;
    }
    if (polygon.type < collision.surfaceTypes.size() &&
        readSurfaceFields(collision.surfaceTypes[polygon.type]).conveyorSpeed != 0) {
        return std::nullopt;
    }
    return RequiresConveyorSpeed;
}

} // namespace toki::scene
