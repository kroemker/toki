#include "ShaderCode.h"

#include "gfx/Types.h"
#include "gfx/UbershaderCode.h"

#include <span>

namespace gfx {

std::span<const u8> findShaderCode(const ShaderFormat format, const ShaderStage stage) {
    const bool vertex = stage == ShaderStage::Vertex;
    switch (format) {
    case ShaderFormat::Spirv:
        return vertex ? std::span<const u8>(shaders::VertexSpirv)
                      : std::span<const u8>(shaders::FragmentSpirv);
    case ShaderFormat::Dxil:
        return vertex ? std::span<const u8>(shaders::VertexDxil)
                      : std::span<const u8>(shaders::FragmentDxil);
    case ShaderFormat::Msl:
        return vertex ? std::span<const u8>(shaders::VertexMsl)
                      : std::span<const u8>(shaders::FragmentMsl);
    }
    return {};
}

} // namespace gfx
