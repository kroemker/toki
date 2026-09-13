#pragma once

#include <imgui.h>

namespace toki::core {

namespace space {

inline constexpr float Tiny = 2.0F;
inline constexpr float Small = 4.0F;
inline constexpr float Medium = 8.0F;
inline constexpr float Large = 12.0F;
inline constexpr float Huge = 20.0F;

} // namespace space

namespace palette {

inline constexpr ImVec4 Background{0.086F, 0.090F, 0.102F, 1.0F};
inline constexpr ImVec4 Surface{0.110F, 0.118F, 0.133F, 1.0F};
inline constexpr ImVec4 SurfaceRaised{0.141F, 0.153F, 0.173F, 1.0F};
inline constexpr ImVec4 SurfaceHovered{0.180F, 0.196F, 0.220F, 1.0F};
inline constexpr ImVec4 SurfaceActive{0.220F, 0.239F, 0.271F, 1.0F};
inline constexpr ImVec4 Border{0.200F, 0.216F, 0.243F, 1.0F};

inline constexpr ImVec4 Text{0.847F, 0.867F, 0.902F, 1.0F};
inline constexpr ImVec4 TextMuted{0.518F, 0.553F, 0.604F, 1.0F};
inline constexpr ImVec4 TextDisabled{0.373F, 0.396F, 0.435F, 1.0F};

inline constexpr ImVec4 Accent{0.353F, 0.639F, 0.949F, 1.0F};
inline constexpr ImVec4 AccentHovered{0.435F, 0.702F, 0.976F, 1.0F};
inline constexpr ImVec4 AccentMuted{0.353F, 0.639F, 0.949F, 0.290F};

inline constexpr ImVec4 Success{0.361F, 0.788F, 0.478F, 1.0F};
inline constexpr ImVec4 Warning{0.910F, 0.718F, 0.314F, 1.0F};
inline constexpr ImVec4 Danger{0.949F, 0.408F, 0.369F, 1.0F};

} // namespace palette

void applyTheme();

} // namespace toki::core
