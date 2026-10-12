#include "n64/CombineMode.h"
#include "n64/CommandDecoder.h"
#include "n64/Opcode.h"
#include "n64/Types.h"

#include <array>
#include <string_view>

namespace n64 {

namespace {

struct InputText {
    std::string_view name;
    std::string_view description;
};

constexpr std::array<InputText, 21> InputTexts{{
    {"COMBINED", "What the first cycle made; in the first cycle, nothing useful."},
    {"TEXEL0", "The colour of the texture the triangle's tile reads."},
    {"TEXEL1", "The colour of the texture of the tile after it, as for a second texture or "
               "the next mipmap level."},
    {"PRIMITIVE", "The primitive colour gsDPSetPrimColor set."},
    {"SHADE", "The colour of the vertices, lit or as given, blended across the triangle."},
    {"ENVIRONMENT", "The environment colour gsDPSetEnvColor set."},
    {"1", "One: the input at full strength."},
    {"0", "Zero: nothing."},
    {"NOISE", "Random noise, different for every pixel."},
    {"CENTER", "The chroma key's centre."},
    {"SCALE", "The chroma key's scale."},
    {"K4", "The fourth YUV conversion constant."},
    {"K5", "The fifth YUV conversion constant."},
    {"COMBINED_ALPHA", "The alpha the first cycle made."},
    {"TEXEL0_ALPHA", "The texture's alpha, used as a colour."},
    {"TEXEL1_ALPHA", "The second texture's alpha."},
    {"PRIMITIVE_ALPHA", "The primitive colour's alpha."},
    {"SHADE_ALPHA", "The vertices' alpha, which fog can stand in for."},
    {"ENV_ALPHA", "The environment colour's alpha."},
    {"LOD_FRACTION", "How far a pixel is between two mipmap levels."},
    {"PRIM_LOD_FRAC", "The level of detail fraction gsDPSetPrimColor set."},
}};

struct BitText {
    u32 bit;
    std::string_view description;
};

constexpr std::array<BitText, 9> GeometryTexts{{
    {geometry::ZBuffer, "Triangles are depth tested and write their depth, when the other mode "
                        "says so too, so nearer surfaces hide farther ones."},
    {geometry::Shade, "Vertices carry a shade colour into the triangle, which the combiner can "
                      "use as SHADE."},
    {geometry::CullFront, "Triangles facing the camera are not drawn."},
    {geometry::CullBack, "Triangles facing away from the camera are not drawn, as the back of a "
                         "closed model is never seen."},
    {geometry::Fog, "Vertices get a fog value from their depth, which the blender mixes the fog "
                    "colour in by."},
    {geometry::Lighting, "Vertices are lit: their normals are shaded by the lights, rather than "
                         "taking their colour as given."},
    {geometry::TextureGen, "Texture coordinates are made from the normals, for shiny surfaces "
                           "that reflect a texture."},
    {geometry::TextureGenLinear, "The texture coordinates made from normals are spread "
                                 "linearly."},
    {geometry::SmoothShading, "Shade is blended across a triangle between its vertices, rather "
                              "than the first vertex's for all of it."},
}};

struct ModeText {
    bool isHigh;
    u8 shift;
    std::string_view description;
};

constexpr std::array<ModeText, 14> ModeTexts{{
    {true, 4, "How alpha is dithered: patterned noise that hides banding."},
    {true, 6, "How colour is dithered when the frame buffer holds fewer bits than the colour."},
    {true, 8, "Whether the combiner keys out the chroma key's colour."},
    {true, 9, "Whether texels are converted from YUV, and filtered before or after."},
    {true, 12,
     "How texels are filtered: point sampled, or bilinear blended with their "
     "neighbours."},
    {true, 14,
     "Whether textures are palette indices, and whether the palette holds RGBA or IA "
     "colours."},
    {true, 16, "Whether the level of detail is computed, for mipmapped textures."},
    {true, 17, "Whether the texture levels are clamped, sharpened or detailed."},
    {true, 19,
     "Whether texture coordinates are corrected for perspective, as on any 3D "
     "surface."},
    {true, 20,
     "The cycle type: one cycle, two cycles for combining twice, copy for drawing "
     "texels as they are, or fill for clearing."},
    {true, 23, "Whether primitives are drawn one at a time or in a pipeline."},
    {false, 0,
     "Whether pixels are dropped by their alpha, against the blend colour's or a "
     "random threshold, as for cut-out leaves and fences."},
    {false, 2, "Where a pixel's depth comes from: the triangle, or the primitive depth."},
    {false, 3,
     "The render mode: how the blender mixes a pixel with what is in the frame "
     "buffer, and how depth, coverage and antialiasing are handled. Opaque, "
     "translucent and decal surfaces each have their own."},
}};

} // namespace

std::string_view getCombinerInputName(CombinerInput input, const bool isAlpha) {
    if (isAlpha) {
        switch (input) {
        case CombinerInput::CombinedAlpha:
            input = CombinerInput::Combined;
            break;
        case CombinerInput::Texel0Alpha:
            input = CombinerInput::Texel0;
            break;
        case CombinerInput::Texel1Alpha:
            input = CombinerInput::Texel1;
            break;
        case CombinerInput::PrimitiveAlpha:
            input = CombinerInput::Primitive;
            break;
        case CombinerInput::ShadeAlpha:
            input = CombinerInput::Shade;
            break;
        case CombinerInput::EnvironmentAlpha:
            input = CombinerInput::Environment;
            break;
        default:
            break;
        }
    }
    const auto index = static_cast<usize>(input);
    return index < InputTexts.size() ? InputTexts[index].name : std::string_view("?");
}

std::string_view getCombinerInputDescription(const CombinerInput input) {
    const auto index = static_cast<usize>(input);
    return index < InputTexts.size() ? InputTexts[index].description : std::string_view();
}

std::string_view getGeometryModeDescription(const u32 bit) {
    for (const BitText& text : GeometryTexts) {
        if (text.bit == bit) {
            return text.description;
        }
    }
    return {};
}

std::string_view getOtherModeDescription(const bool isHigh, const u8 shift) {
    for (const ModeText& text : ModeTexts) {
        if (text.isHigh == isHigh && text.shift == shift) {
            return text.description;
        }
    }
    return {};
}

} // namespace n64
