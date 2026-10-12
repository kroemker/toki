#pragma once

#include "app/tools/configure/Types.h"
#include "app/tools/configure/domain/MemoryLayout.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

namespace toki::configure {

[[nodiscard]] ImVec4 getRegionColor(RegionKind kind);

/// The colour a region is filled with, its kind's, as a swatch beside its row.
void drawRegionSwatch(const MemoryRegion& region);

/// The MiB scale above the strip, with the address of every second MiB and
/// the line where RAM ends without an Expansion Pak.
void drawMemoryRuler();

/// The whole of RDRAM as one strip, a region in its kind's colour where the
/// layout places it, with a line at every MiB. `inspected`, a region of the
/// layout, is outlined, or the region it lies in when it is not drawn. Returns
/// the region under the mouse.
[[nodiscard]] std::optional<usize> drawMemoryStrip(const MemoryLayout& layout,
                                                   std::optional<usize> inspected);

/// A line of what `inspected` is, where its size comes from, and where it
/// lies, or a summary of the layout when nothing is. `mapAge` says how old
/// the build's map is.
void drawMemoryInspector(const MemoryLayout& layout, std::optional<usize> inspected,
                         std::string_view mapAge);

/// `bytes` in the largest unit that keeps it above one: "2.00 MiB", "44.0 KiB".
[[nodiscard]] std::string makeSizeText(s64 bytes);

} // namespace toki::configure
