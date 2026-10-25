#pragma once

#include "z64/DeclarationWriter.h"

namespace cparser {
class TypeTable;
} // namespace cparser

namespace toki::scene {

/// Writes a `CollisionPoly` as fast64 does: its surface type, its vertices as
/// `COLPOLY_VTX` with their flags by name, and its normal as `COLPOLY_SNORMAL`
/// of the shortest fraction that truncates to the stored value.
[[nodiscard]] z64::ElementWriter makeCollisionPolyWriter();

/// Writes a `SurfaceType` as its two words' `SURFACETYPE0` and `SURFACETYPE1`,
/// each field a number.
[[nodiscard]] z64::ElementWriter makeSurfaceTypeWriter();

/// Writes a `WaterBox` with its properties as `WATERBOX_PROPERTIES`.
[[nodiscard]] z64::ElementWriter makeWaterBoxWriter();

/// Writes a `BgCamInfo` with its setting by its `CameraSettingType`
/// enumerator. `types` must outlive the writer.
[[nodiscard]] z64::ElementWriter makeBgCamInfoWriter(const cparser::TypeTable& types);

} // namespace toki::scene
