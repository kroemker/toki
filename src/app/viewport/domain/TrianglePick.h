#pragma once

#include "app/viewport/Types.h"
#include "n64/RspResult.h"
#include "z64/MemoryImage.h"

#include <glm/ext/vector_float2.hpp>

#include <optional>
#include <span>

namespace toki::viewport {

/// The physical address of the command that drew the triangle nearest the
/// camera under `point`, given in normalized device coordinates, y up, of the
/// frame `result` was run for. A triangle with a vertex behind the camera is
/// passed over, as it would have to be clipped first.
[[nodiscard]] std::optional<u32> findPickedTriangle(const n64::RspResult& result, glm::vec2 point);

/// The index of the image of `images` mapped where `address` is, if any.
[[nodiscard]] std::optional<usize>
findImageAt(std::span<const std::optional<z64::MappedImage>> images, u32 address);

} // namespace toki::viewport
