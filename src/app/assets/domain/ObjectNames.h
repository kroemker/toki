#pragma once

#include "hackeroot/Tables.h"

#include <optional>
#include <string>
#include <string_view>

namespace toki::assets {

/// What a vanilla object holds, as a person would call it: `object_kw1` is
/// "Red-Haired Kokiri Girl". The checkout names objects only by file, so these
/// are written down here, after the CloudModding object list. Nothing for an
/// object the game did not ship with.
[[nodiscard]] std::optional<std::string_view> findObjectName(std::string_view folderName);

/// An object of the object table as people read it: what it holds when that is
/// known, or else its enum name as words, `OBJECT_TOKI_TEST` "Toki Test".
[[nodiscard]] std::string makeObjectLabel(const hackeroot::ObjectEntry& object);

} // namespace toki::assets
