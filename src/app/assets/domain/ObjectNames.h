#pragma once

#include <optional>
#include <string_view>

namespace toki::assets {

/// What a vanilla object holds, as a person would call it: `object_kw1` is
/// "Red-Haired Kokiri Girl". The checkout names objects only by file, so these
/// are written down here, after the CloudModding object list. Nothing for an
/// object the game did not ship with.
[[nodiscard]] std::optional<std::string_view> findObjectName(std::string_view folderName);

} // namespace toki::assets
