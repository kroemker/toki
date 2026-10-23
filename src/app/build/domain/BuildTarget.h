#pragma once

#include "app/build/Types.h"

namespace toki::build {

/// What a build is started for: the ROM alone, or the ROM and then the
/// emulator on it.
enum class BuildTarget : u8 {
    Build,
    Run,
};

} // namespace toki::build
