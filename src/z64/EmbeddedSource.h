#pragma once

#include <filesystem>
#include <string>

namespace z64 {

/// The file the build generates an object's whole initializer from, as it makes
/// a texture, a palette or a binary: the include as it was spelled,
/// `assets/scenes/overworld/spot04/spot04_room_0_0000BF08_Tex.rgba16.inc.c`, and
/// the file it resolved to, the `.png`, `.jpg` or `.bin` the build reads.
struct EmbeddedSource {
    std::string spelling;
    std::filesystem::path path;

    [[nodiscard]] bool operator==(const EmbeddedSource&) const = default;
};

} // namespace z64
