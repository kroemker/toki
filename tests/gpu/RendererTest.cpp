#include "gfx/Renderer.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "gfx/Device.h"
#include "gfx/RenderTarget.h"
#include "n64/Blender.h"
#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/Combiner.h"
#include "n64/DisplayListBuilder.h"
#include "n64/Memory.h"
#include "n64/OtherMode.h"
#include "n64/Rdp.h"
#include "n64/Rsp.h"
#include "n64/RspResult.h"
#include "n64/TexelFetch.h"
#include "n64/TileDescriptor.h"

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <expected>
#include <string>
#include <utility>
#include <vector>

using base::f32;
using base::s32;
using base::u16;
using base::u32;
using base::u8;
using base::usize;
using n64::Color;
using n64::test::DisplayListBuilder;

namespace {

constexpr u32 Width = 320;
constexpr u32 Height = 240;
constexpr u32 SegmentBase = 0x0010'0000;
constexpr s32 One = 0x0001'0000;
constexpr u32 TwoCycle = 1U << 20;
constexpr u32 TexturePerspective = 1U << 19;
constexpr u32 FilterBilinear = 2U << 12;
constexpr u32 ForceBlend = 0x4000;
constexpr u32 Smooth = 0x0020'0000;
constexpr u32 Shade = 0x0000'0004;

struct Equation {
    u32 a = 8;
    u32 b = 8;
    u32 c = 31;
    u32 d = 7;
    u32 alphaA = 7;
    u32 alphaB = 7;
    u32 alphaC = 7;
    u32 alphaD = 7;
};

std::array<u32, 2> makeCombineWords(const Equation& first, const Equation& second) {
    return {0xFC00'0000U | (first.a << 20) | (first.c << 15) | (first.alphaA << 12) |
                (first.alphaC << 9) | (second.a << 5) | second.c,
            (first.b << 28) | (second.b << 24) | (second.alphaA << 21) | (second.alphaC << 18) |
                (first.d << 15) | (first.alphaB << 12) | (first.alphaD << 9) | (second.d << 6) |
                (second.alphaB << 3) | second.alphaD};
}

u32 makeBlenderCycle(const u32 first, const u32 firstWeight, const u32 second,
                     const u32 secondWeight) {
    return (first << 30) | (firstWeight << 26) | (second << 22) | (secondWeight << 18);
}

u32 packColor(const Color& color) {
    return (u32{color[0]} << 24) | (u32{color[1]} << 16) | (u32{color[2]} << 8) | color[3];
}

class Scene {
public:
    DisplayListBuilder builder;
    u32 projection = builder.addMatrix(
        {One / 100, 0, 0, 0, 0, One / 100, 0, 0, 0, 0, One / 100, 0, 0, 0, 0, One});
    u32 start = 0;

    void beginCommands() {
        start = builder.applyMatrix(projection, 0x04 | 0x02);
        builder.setGeometryMode(0, Smooth | Shade);
    }

    void setOtherMode(const u32 high, const u32 low) {
        builder.addWords(0xEF00'0000U | high, low);
    }

    void setCombine(const Equation& both) {
        const std::array<u32, 2> words = makeCombineWords(both, both);
        builder.addWords(words[0], words[1]);
    }

    void setCombine(const Equation& first, const Equation& second) {
        const std::array<u32, 2> words = makeCombineWords(first, second);
        builder.addWords(words[0], words[1]);
    }

    void drawScreen(const u32 vertices) {
        builder.loadVertices(vertices, 4, 0);
        builder.drawTriangles({0, 1, 2}, {1, 3, 2});
    }

    [[nodiscard]] u32 addScreenVertices(const Color& color) {
        const u32 address = builder.addVertex({-200, -200, 0}, {0, 0}, color);
        builder.addVertex({200, -200, 0}, {0, 0}, color);
        builder.addVertex({-200, 200, 0}, {0, 0}, color);
        builder.addVertex({200, 200, 0}, {0, 0}, color);
        return address;
    }
};

class Gpu {
public:
    static Gpu create() {
        std::expected<gfx::Device, base::Diagnostic> device = gfx::Device::create();
        if (!device) {
            SKIP(device.error().message);
        }
        return Gpu(std::move(*device));
    }

    struct Frame {
        n64::RspResult rsp;
        n64::RdpResult rdp;
        std::vector<u8> pixels;
        std::vector<base::Diagnostic> diagnostics;

        [[nodiscard]] Color getPixel(const u32 x, const u32 y) const {
            const usize offset = ((usize{y} * Width) + x) * 4;
            return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
        }
    };

    [[nodiscard]] Frame draw(Scene& scene) {
        scene.builder.endDisplayList();
        n64::Memory memory;
        REQUIRE(memory.map(SegmentBase, scene.builder.getBytes()));
        n64::RspTask task{.displayList = scene.start};
        task.segments[DisplayListBuilder::Segment] = SegmentBase;
        Frame frame;
        frame.rsp = n64::runTask(memory, task);
        frame.rdp = n64::runRdp(frame.rsp, memory);
        std::expected<gfx::RenderTarget, base::Diagnostic> target =
            gfx::RenderTarget::create(device, Width, Height);
        REQUIRE(target.has_value());
        std::expected<gfx::Renderer, base::Diagnostic> renderer = gfx::Renderer::create(device);
        REQUIRE(renderer.has_value());
        frame.diagnostics = renderer->render(frame.rsp, frame.rdp, *target);
        std::expected<std::vector<u8>, base::Diagnostic> pixels = target->readPixels(device);
        REQUIRE(pixels.has_value());
        frame.pixels = std::move(*pixels);
        return frame;
    }

private:
    explicit Gpu(gfx::Device device) : device(std::move(device)) {}

    gfx::Device device;
};

} // namespace

TEST_CASE("a shaded triangle covers the screen in its colour", "[gpu]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    const u32 vertices = scene.addScreenVertices({10, 200, 30, 255});
    scene.beginCommands();
    scene.setOtherMode(0, 0);
    scene.setCombine({.d = 4, .alphaD = 4});
    scene.drawScreen(vertices);

    const Gpu::Frame frame = gpu.draw(scene);

    REQUIRE(frame.diagnostics.empty());
    REQUIRE(frame.getPixel(160, 120) == Color{10, 200, 30, 255});
    REQUIRE(frame.getPixel(0, 0) == Color{10, 200, 30, 255});
}

TEST_CASE("the shader combines exactly as evaluateCombiner does", "[gpu]") {
    Gpu gpu = Gpu::create();
    const Color shade{200, 150, 100, 180};
    const Color primitive{90, 255, 40, 128};
    const Color environment{255, 30, 160, 64};
    const std::array<std::array<Equation, 2>, 5> modes{{
        {Equation{.a = 3, .c = 4, .alphaA = 3, .alphaC = 4}, Equation{}},
        {Equation{
             .a = 3, .b = 5, .c = 4, .d = 5, .alphaA = 3, .alphaB = 5, .alphaC = 4, .alphaD = 5},
         Equation{}},
        {Equation{.a = 6, .c = 4, .d = 4, .alphaD = 4}, Equation{}},
        {Equation{.a = 4, .c = 3, .alphaA = 4, .alphaC = 3},
         Equation{.a = 0, .b = 5, .c = 10, .d = 5, .alphaA = 0, .alphaC = 5}},
        {Equation{.a = 5, .b = 3, .c = 12, .d = 3, .alphaD = 6},
         Equation{.a = 0, .c = 4, .alphaA = 0, .alphaC = 4}},
    }};
    for (usize i = 0; i < modes.size(); ++i) {
        const bool twoCycle = i >= 3;
        const n64::OtherMode otherMode{.high = twoCycle ? TwoCycle : 0, .low = 0};
        Scene scene;
        const u32 vertices = scene.addScreenVertices(shade);
        scene.beginCommands();
        scene.setOtherMode(otherMode.high, 0);
        scene.builder.addWords(0xFA00'0000, packColor(primitive));
        scene.builder.addWords(0xFB00'0000, packColor(environment));
        if (twoCycle) {
            scene.setCombine(modes[i][0], modes[i][1]);
        } else {
            scene.setCombine(modes[i][0]);
        }
        scene.drawScreen(vertices);

        const Gpu::Frame frame = gpu.draw(scene);

        const n64::CombineMode combineMode = frame.rdp.states.back().combineMode;
        const Color expected =
            n64::evaluateCombiner(
                combineMode, otherMode,
                {.primitive = primitive, .shade = shade, .environment = environment})
                .color;
        INFO("mode " << i);
        const Color actual = frame.getPixel(160, 120);
        REQUIRE(actual[0] == expected[0]);
        REQUIRE(actual[1] == expected[1]);
        REQUIRE(actual[2] == expected[2]);
    }
}

TEST_CASE("texels drawn one to a pixel are the texels fetchTexel reads", "[gpu]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    std::vector<u8> texels;
    for (u32 i = 0; i < 16; ++i) {
        const u32 value = ((i * 2) << 11) | ((31 - (i * 2)) << 6) | ((i * 17 % 32) << 1) | 1;
        texels.push_back(static_cast<u8>(value >> 8));
        texels.push_back(static_cast<u8>(value));
    }
    const u32 texture = scene.builder.getAddress();
    for (const u8 byte : texels) {
        scene.builder.addBytes({byte});
    }
    scene.beginCommands();
    scene.setOtherMode(0, 0);
    scene.setCombine({.d = 1, .alphaD = 1});
    scene.builder.addWords(0xFD10'0003, texture);
    scene.builder.addWords(0xF510'0000, 0x0700'0000);
    scene.builder.addWords(0xF300'0000, 0x0700'F800);
    scene.builder.addWords(0xF510'0200, 0x0000'0000);
    scene.builder.addWords(0xF200'0000, 0x0000'C00C);
    scene.builder.addWords(0xE404'8048, 0x0002'0020);
    scene.builder.addWords(0xE100'0000, 0x0000'0000);
    scene.builder.addWords(0xF100'0000, 0x0400'0400);

    const Gpu::Frame frame = gpu.draw(scene);

    REQUIRE(frame.diagnostics.empty());
    const n64::RdpState& state = frame.rdp.states.back();
    for (s32 y = 0; y < 4; ++y) {
        for (s32 x = 0; x < 4; ++x) {
            const Color expected = n64::fetchTexel(frame.rdp.textureMemories.back(), state.tiles[0],
                                                   {x, y}, n64::TlutMode::None);
            INFO("texel " << x << ", " << y);
            REQUIRE(frame.getPixel(static_cast<u32>(8 + x), static_cast<u32>(8 + y)) == expected);
        }
    }
}

TEST_CASE("fog in the first blender cycle matches evaluateBlender", "[gpu]") {
    Gpu gpu = Gpu::create();
    const Color shade{64, 128, 192, 100};
    const Color fog{255, 0, 0, 255};
    const u32 fogCycle = makeBlenderCycle(3, 2, 0, 0) | (1U << 20) | (1U << 16);
    const n64::OtherMode otherMode{.high = TwoCycle, .low = fogCycle};
    Scene scene;
    const u32 vertices = scene.addScreenVertices(shade);
    scene.beginCommands();
    scene.setOtherMode(otherMode.high, otherMode.low);
    scene.builder.addWords(0xF800'0000, packColor(fog));
    scene.setCombine({.d = 4, .alphaD = 4}, {.d = 0, .alphaD = 0});
    scene.drawScreen(vertices);

    const Gpu::Frame frame = gpu.draw(scene);

    REQUIRE(frame.diagnostics.empty());
    const std::array<u8, 3> expected =
        n64::evaluateBlender(otherMode, {.pixel = shade, .fog = fog, .shadeAlpha = shade[3]});
    const Color actual = frame.getPixel(160, 120);
    REQUIRE(actual[0] == expected[0]);
    REQUIRE(actual[1] == expected[1]);
    REQUIRE(actual[2] == expected[2]);
}

TEST_CASE("translucency over the framebuffer comes close to the RDP's", "[gpu]") {
    Gpu gpu = Gpu::create();
    const Color below{255, 0, 0, 255};
    const Color above{0, 0, 255, 128};
    const n64::OtherMode translucent{.high = 0, .low = ForceBlend | makeBlenderCycle(0, 0, 1, 0)};
    Scene scene;
    const u32 opaque = scene.addScreenVertices(below);
    const u32 glass = scene.addScreenVertices(above);
    scene.beginCommands();
    scene.setOtherMode(0, 0);
    scene.setCombine({.d = 4, .alphaD = 4});
    scene.drawScreen(opaque);
    scene.setOtherMode(0, translucent.low);
    scene.drawScreen(glass);

    const Gpu::Frame frame = gpu.draw(scene);

    REQUIRE(frame.diagnostics.empty());
    const std::array<u8, 3> expected =
        n64::evaluateBlender(translucent, {.pixel = above, .memory = below});
    const Color actual = frame.getPixel(160, 120);
    for (usize channel = 0; channel < 3; ++channel) {
        REQUIRE(std::abs(actual[channel] - expected[channel]) <= 3);
    }
}

TEST_CASE("an anti-aliased edge cutout leaves what is below its transparent pixels", "[gpu]") {
    Gpu gpu = Gpu::create();
    const Color below{255, 0, 0, 255};
    const u32 texEdge = 0x3048 | makeBlenderCycle(0, 0, 1, 1);
    const n64::OtherMode cutout{.high = 0, .low = texEdge};
    for (const u8 alpha : {u8{0}, u8{31}, u8{32}, u8{255}}) {
        const Color above{0, 0, 255, alpha};
        Scene scene;
        const u32 opaque = scene.addScreenVertices(below);
        const u32 edge = scene.addScreenVertices(above);
        scene.beginCommands();
        scene.setOtherMode(0, 0);
        scene.setCombine({.d = 4, .alphaD = 4});
        scene.drawScreen(opaque);
        scene.setOtherMode(0, cutout.low);
        scene.drawScreen(edge);

        const Gpu::Frame frame = gpu.draw(scene);

        REQUIRE(frame.diagnostics.empty());
        const n64::CombinerResult combined =
            n64::evaluateCombiner(frame.rdp.states.back().combineMode, cutout, {.shade = above});
        const Color actual = frame.getPixel(160, 120);
        INFO("alpha " << int{alpha});
        if (n64::isPixelWritten(cutout, combined.coverage)) {
            REQUIRE(actual[2] == 255);
        } else {
            REQUIRE(actual[0] == 255);
            REQUIRE(actual[2] == 0);
        }
    }
}

TEST_CASE("a lit, textured, fogged two-cycle scene renders", "[gpu]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    std::vector<u8> checker;
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 8; ++x) {
            const u16 value = ((x + y) % 2) == 0 ? 0xFFFF : 0x07C1;
            checker.push_back(static_cast<u8>(value >> 8));
            checker.push_back(static_cast<u8>(value));
        }
    }
    const u32 texture = scene.builder.getAddress();
    for (const u8 byte : checker) {
        scene.builder.addBytes({byte});
    }
    const u32 light =
        scene.builder.addBytes({255, 220, 180, 0, 255, 220, 180, 0, 40, 40, 90, 0, 0, 0, 0, 0});
    const u32 ambient =
        scene.builder.addBytes({40, 40, 60, 0, 40, 40, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    const u32 triangle = scene.builder.addVertex({-150, -100, 0}, {0, 0}, {0, 0, 127, 255});
    scene.builder.addVertex({150, -100, 0}, {8 * 32 * 4, 0}, {60, 0, 110, 255});
    scene.builder.addVertex({0, 120, 0}, {4 * 32 * 4, 8 * 32 * 4}, {0, 80, 100, 255});

    scene.beginCommands();
    scene.setOtherMode(TwoCycle | TexturePerspective | FilterBilinear,
                       makeBlenderCycle(3, 2, 0, 0) | (1U << 20) | (1U << 16));
    scene.builder.addWords(0xF800'0000, 0x3050'90FF);
    scene.builder.moveWord(0x08, 0, 100);
    scene.setCombine({.a = 1, .c = 4, .alphaA = 1, .alphaC = 4}, {.d = 0, .alphaD = 0});
    scene.builder.moveWord(0x02, 0, 24);
    scene.builder.moveMemory(light, 16, 10, 0x30);
    scene.builder.moveMemory(ambient, 16, 10, 0x48);
    scene.builder.setGeometryMode(0, 0x0002'0000 | 0x0001'0000);
    scene.builder.setTexture(0x4000, 0x4000, 0, true);
    scene.builder.addWords(0xFD10'0007, texture);
    scene.builder.addWords(0xF510'0000, 0x0700'0000);
    scene.builder.addWords(0xF300'0000, 0x0703'F400);
    scene.builder.addWords(0xF510'0400, 0x0000'0000);
    scene.builder.addWords(0xF200'0000, 0x0001'C01C);
    scene.builder.loadVertices(triangle, 3, 0);
    scene.builder.drawTriangle(0, 1, 2);

    const Gpu::Frame frame = gpu.draw(scene);

    REQUIRE(frame.rsp.diagnostics.empty());
    REQUIRE(frame.diagnostics.empty());
    REQUIRE(frame.getPixel(160, 120) != Color{0, 0, 0, 0});
    REQUIRE(frame.getPixel(5, 5) == Color{0, 0, 0, 0});

    std::vector<u8> pixels = frame.pixels;
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(static_cast<int>(Width), static_cast<int>(Height),
                              SDL_PIXELFORMAT_ABGR8888, pixels.data(), static_cast<int>(Width * 4));
    REQUIRE(surface != nullptr);
    const std::string path = std::string(TOKI_GPU_OUTPUT) + "/two_cycle_scene.bmp";
    REQUIRE(SDL_SaveBMP(surface, path.c_str()));
    SDL_DestroySurface(surface);
}

namespace {

constexpr u32 TlutRgba16 = 2U << 14;
constexpr u32 FilterAverage = 3U << 12;
constexpr u32 TextureLoadTile = 7;

struct TileWords {
    u32 format = 0;
    u32 size = 2;
    u32 line = 0;
    u32 address = 0;
    u32 palette = 0;
    u32 axisS = 0;
    u32 axisT = 0;
};

std::array<u32, 2> setTextureImage(const u32 format, const u32 size, const u32 width,
                                   const u32 address) {
    return {0xFD00'0000U | (format << 21) | (size << 19) | (width - 1), address};
}

std::array<u32, 2> setTile(const u32 tile, const TileWords& words) {
    return {0xF500'0000U | (words.format << 21) | (words.size << 19) | (words.line << 9) |
                words.address,
            (tile << 24) | (words.palette << 20) | (words.axisT << 10) | words.axisS};
}

std::array<u32, 2> setTileSize(const u32 tile, const u32 width, const u32 height) {
    return {0xF200'0000U, (tile << 24) | (((width - 1) << 2) << 12) | ((height - 1) << 2)};
}

std::array<u32, 2> loadBlock(const u32 texels, const u32 dxt) {
    return {0xF300'0000U, (TextureLoadTile << 24) | ((texels - 1) << 12) | dxt};
}

std::array<u32, 2> loadTile(const u32 width, const u32 height) {
    return {0xF400'0000U,
            (TextureLoadTile << 24) | (((width - 1) << 2) << 12) | ((height - 1) << 2)};
}

std::array<u32, 2> loadTlut(const u32 entries) {
    return {0xF000'0000U, (TextureLoadTile << 24) | ((entries - 1) << 14)};
}

u32 makeAxis(const bool clamp, const bool mirror, const u32 mask) {
    return ((clamp ? 2U : 0U) << 8) | ((mirror ? 1U : 0U) << 8) | (mask << 4);
}

struct TextureDraw {
    u32 otherModeHigh = 0;
    f32 step = 1.0F;
    f32 origin = 0.0F;
};

constexpr u32 RectangleCorner = 16;
constexpr u32 RectangleSize = 16;

void requireTextureMatches(Gpu& gpu, Scene& scene, const std::vector<std::array<u32, 2>>& commands,
                           const TextureDraw& draw) {
    scene.beginCommands();
    scene.setOtherMode(draw.otherModeHigh, 0);
    scene.setCombine({.d = 1, .alphaD = 1});
    for (const std::array<u32, 2>& command : commands) {
        scene.builder.addWords(command[0], command[1]);
    }
    const u32 near = RectangleCorner * 4;
    const u32 far = (RectangleCorner + RectangleSize) * 4;
    const auto origin = static_cast<u32>(static_cast<s32>(draw.origin * 32.0F)) & 0xFFFF;
    const auto step = static_cast<u32>(draw.step * 1024.0F) & 0xFFFF;
    scene.builder.addWords(0xE400'0000U | (far << 12) | far, (near << 12) | near);
    scene.builder.addWords(0xE100'0000U, (origin << 16) | origin);
    scene.builder.addWords(0xF100'0000U, (step << 16) | step);

    const Gpu::Frame frame = gpu.draw(scene);

    REQUIRE(frame.diagnostics.empty());
    REQUIRE(frame.rdp.diagnostics.empty());
    const n64::RdpState& state = frame.rdp.states.back();
    for (u32 y = 0; y < RectangleSize; ++y) {
        for (u32 x = 0; x < RectangleSize; ++x) {
            const std::array<f32, 2> coordinate{draw.origin + (static_cast<f32>(x) * draw.step),
                                                draw.origin + (static_cast<f32>(y) * draw.step)};
            const Color expected = n64::sampleTexture(
                frame.rdp.textureMemories.back(), state.tiles[0], coordinate,
                state.otherMode.getTlutMode(), state.otherMode.getTextureFilter());
            INFO("pixel " << x << ", " << y);
            REQUIRE(frame.getPixel(RectangleCorner + x, RectangleCorner + y) == expected);
        }
    }
}

u32 addBytes(Scene& scene, const std::vector<u8>& bytes) {
    const u32 address = scene.builder.getAddress();
    for (const u8 byte : bytes) {
        scene.builder.addBytes({byte});
    }
    return address;
}

std::vector<u8> makeRgba16Texture(const u32 texels) {
    std::vector<u8> bytes;
    for (u32 i = 0; i < texels; ++i) {
        const u32 value = (((i * 7) % 32) << 11) | (((31 - i) % 32) << 6) | (((i * 13) % 32) << 1) |
                          (i % 3 == 0 ? 0U : 1U);
        bytes.push_back(static_cast<u8>(value >> 8));
        bytes.push_back(static_cast<u8>(value));
    }
    return bytes;
}

std::vector<u8> makeRamp(const u32 count, const u32 stride) {
    std::vector<u8> bytes;
    bytes.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        bytes.push_back(static_cast<u8>((i * stride) + 3));
    }
    return bytes;
}

} // namespace

TEST_CASE("bilinear filtering on the GPU matches sampleTexture", "[gpu][texture]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    const u32 texture = addBytes(scene, makeRgba16Texture(16));
    const TileWords rgba16{.size = 2, .line = 1};

    requireTextureMatches(gpu, scene,
                          {setTextureImage(0, 2, 4, texture), setTile(TextureLoadTile, rgba16),
                           loadBlock(16, 0x800), setTile(0, rgba16), setTileSize(0, 4, 4)},
                          {.otherModeHigh = FilterBilinear, .step = 0.25F});
}

TEST_CASE("average filtering on the GPU matches sampleTexture", "[gpu][texture]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    const u32 texture = addBytes(scene, makeRgba16Texture(16));
    const TileWords rgba16{.size = 2, .line = 1};

    requireTextureMatches(gpu, scene,
                          {setTextureImage(0, 2, 4, texture), setTile(TextureLoadTile, rgba16),
                           loadBlock(16, 0x800), setTile(0, rgba16), setTileSize(0, 4, 4)},
                          {.otherModeHigh = FilterAverage, .step = 0.5F, .origin = 0.5F});
}

TEST_CASE("a mirrored, paletted 4-bit texture on the GPU matches sampleTexture", "[gpu][texture]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    const u32 palette = addBytes(scene, makeRgba16Texture(16));
    const u32 texture = addBytes(scene, makeRamp(16, 0x37));

    requireTextureMatches(gpu, scene,
                          {setTextureImage(0, 2, 1, palette),
                           setTile(TextureLoadTile, {.address = 256}), loadTlut(16),
                           setTextureImage(2, 2, 1, texture),
                           setTile(TextureLoadTile, {.format = 2, .size = 2}), loadBlock(8, 0x800),
                           setTile(0, {.format = 2,
                                       .size = 0,
                                       .line = 1,
                                       .axisS = makeAxis(false, true, 3),
                                       .axisT = makeAxis(false, false, 1)}),
                           setTileSize(0, 16, 2)},
                          {.otherModeHigh = TlutRgba16});
}

TEST_CASE("wrapped 8-bit intensity-alpha on the GPU matches sampleTexture", "[gpu][texture]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    const u32 texture = addBytes(scene, makeRamp(16, 0x1D));

    requireTextureMatches(gpu, scene,
                          {setTextureImage(3, 2, 1, texture),
                           setTile(TextureLoadTile, {.format = 3, .size = 2}), loadBlock(8, 0x800),
                           setTile(0, {.format = 3,
                                       .size = 1,
                                       .line = 1,
                                       .axisS = makeAxis(true, false, 0),
                                       .axisT = makeAxis(false, false, 1)}),
                           setTileSize(0, 8, 2)},
                          {});
}

TEST_CASE("a tile load's odd rows on the GPU match sampleTexture", "[gpu][texture]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    const u32 texture = addBytes(scene, makeRamp(64, 0x0B));
    const TileWords intensityAlpha16{.format = 3, .size = 2, .line = 2};

    requireTextureMatches(gpu, scene,
                          {setTextureImage(3, 2, 8, texture),
                           setTile(TextureLoadTile, intensityAlpha16), loadTile(8, 4),
                           setTile(0, intensityAlpha16), setTileSize(0, 8, 4)},
                          {});
}

TEST_CASE("32-bit RGBA on the GPU matches sampleTexture", "[gpu][texture]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    const u32 texture = addBytes(scene, makeRamp(16, 0x2F));
    const TileWords rgba32{.format = 0, .size = 3, .line = 1};

    requireTextureMatches(gpu, scene,
                          {setTextureImage(0, 3, 2, texture), setTile(TextureLoadTile, rgba32),
                           loadTile(2, 2), setTile(0, rgba32), setTileSize(0, 2, 2)},
                          {.otherModeHigh = FilterBilinear, .step = 0.25F});
}

TEST_CASE("a fill rectangle into a 32-bit image fills with the whole colour", "[gpu]") {
    Gpu gpu = Gpu::create();
    Scene scene;
    scene.beginCommands();
    scene.builder.addWords(0xFF18'013F, 0x0020'0000);
    scene.setOtherMode(3U << 20, 0);
    scene.builder.addWords(0xF700'0000, 0x1234'56FF);
    scene.builder.addWords(0xF600'0000U | ((40U * 4) << 12) | (40U * 4),
                           ((8U * 4) << 12) | (8U * 4));

    const Gpu::Frame frame = gpu.draw(scene);

    REQUIRE(frame.diagnostics.empty());
    REQUIRE(frame.getPixel(20, 20) == Color{0x12, 0x34, 0x56, 0xFF});
}
