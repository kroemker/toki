#pragma once

#include <array>
#include <string_view>

namespace hackeroot {

/// HackerOoT's feature switches that data may stand under in an asset file a
/// tool writes whole: that data is written under its `#if` again, and a file
/// holding data under a switch that is off is not written, because that data
/// was never read and would be lost.
inline constexpr std::array<std::string_view, 4> FeatureSwitches{
    "ENABLE_ANIMATED_MATERIALS", "ENABLE_CUTSCENE_IMPROVEMENTS", "ENABLE_MM_TITLE_CARDS",
    "ENABLE_F3DEX3"};

} // namespace hackeroot
