# The renderer

Why the renderer is our own, what it takes from the renderers that exist, and how its
stages divide the work. The goal is an accurate F3DEX2 renderer that we can extend in any
direction: inspection, the actor render database, display lists built at runtime.

## What was evaluated

**Fast3D** (libultraship `759b134`, now `src/fast/`, MIT). Its display lists are PC
display lists: `F3DGfx` is two `uintptr_t`, 16 bytes a command on a 64-bit host, and an
address operand is a host pointer unless bit 0 marks it segmented
(`segments[w1 >> 24] + (w1 & 0xFFFFFE)`). A real segment address such as `0x06001234` has
bit 0 clear, so N64 words cannot be fed to it without a translation pass that doubles every
command and relocates every `G_DL`. Vertices, matrices and lights are read as host-endian
structs. It approximates the combiner, mostly ignores the blender and does not emulate
texture memory. Its OpenGL backend loads shaders through libultraship's resource system
and the prism template processor, and the interpreter is woven through with
`Ship::ResourceManager`, console variables, the GUI and spdlog. No F3DEX3.

**RT64** (`4337374`, MIT). Accurate: texture memory, framebuffer tracking, the RDP's
combiner and blender. It reads real N64 memory, but as an emulator plugin: a struct with an
8 MB RDRAM pointer, RSP memory and registers. Backends are D3D12, Vulkan and Metal through
its own `plume` layer, it ships its own Dear ImGui, and it has sixteen submodules (DXC,
SPIRV-Cross, XeSS, mupen64plus-core among them). Microcodes F3D, F3DEX, F3DEX2 and
several game variants. No F3DEX3.

**parallel-rdp** (MIT). Pixel-exact low-level RDP emulation in Vulkan compute. Needs an
emulated RSP to produce RDP commands. Not embeddable as a renderer, but the reference for
what the RDP does to a pixel.

**GLideN64** (GPL). A mature high-level renderer. Read for understanding, never copied.

None fits: Fast3D fights the memory image and is not accurate enough, RT64 is accurate
but is not ours to extend and would bring a second ImGui and a backend layer. So the
renderer is written here, and the three MIT renderers are references. Code taken from
them keeps its copyright notice in `THIRD_PARTY_NOTICES.md`.

## What is taken from where

- **RT64** — texture memory emulation, the RDP state model, how combiner and blender state
  becomes shader input, F3DEX2 command handling. The closest reference overall.
- **Fast3D** — combiner-to-shader mapping, the edge cases of `G_MOVEWORD`, `G_MOVEMEM` and
  lights.
- **parallel-rdp** — the exact arithmetic of the combiner, blender, coverage and
  dithering, when pixels are compared.
- **N64brew wiki and the SDK manuals** — the specification itself.

## Stages

The N64 renders in two processors, and so does this.

1. **RSP** (`n64/`, CPU). F3DEX2 commands in, triangles out. Matrix stack, vertex
   transform and lighting in the N64's fixed point, texture coordinate generation,
   clipping, culling, segment resolution, display list calls. It reads words out of a
   segment table over the memory image; it never sees C or `gbi.h`, and knows the opcodes
   as hardware facts.
2. **RDP state** (`n64/`, CPU). Texture memory loads, tile descriptors, palettes, combiner
   and blender settings. Each triangle leaves the CPU stages with the full state it is
   drawn with.
3. **GPU** (`gfx/`). The triangle list goes through SDL3's GPU API. One ubershader reads
   the combiner and blender settings as data, so nothing is generated or compiled at
   runtime and the shader is compiled once per backend at build time.

**Blending that reads the framebuffer uses the GPU's own blend modes.** SDL3's GPU API
cannot read the framebuffer from a shader, and the blender's memory colour is exactly
that. The modes games use (translucency, additive, opaque over memory) have exact
fixed-function equivalents; a blender cycle that reads no memory, such as fog in the
first cycle, runs in the shader with the RDP's arithmetic. What is lost is the last bit
of exactness in the memory blend: five-bit weights and wrap-around become the GPU's
blending. A mode with no equivalent is a diagnostic, not a guess. Copying the framebuffer
before every translucent draw, or rasterising in compute as parallel-rdp does, would be
exact, and were turned down as too costly for a viewer.

Stages 1 and 2 are in `toki_core` and are tested the way parsers are. Only the shader's
arithmetic needs a pixel comparison, against RT64 or an emulator screenshot until a
parallel-rdp harness exists.

## The RSP stage

`n64::runTask` runs one graphics task against a `Memory`, the physical address space, and
a starting segment table. It produces what the RSP sends the RDP: triangles with their
vertices transformed, lit, fogged and textured, texture rectangles, and every other RDP
command as it is, with image addresses resolved. Triangles stay in clip space, because
the GPU clips them; the viewport each was transformed under travels with it.

It follows F3DEX2's own layouts where that keeps it exact: lights are kept as the 24-byte
slots `G_MOVEMEM` and `G_MOVEWORD` write into, so a partial write behaves as it does on
the RSP, and matrices are kept in s15.16, so `G_MW_MATRIX` can overwrite half an element.

Approximated, and to be checked against a reference:

- Matrix products round toward negative infinity; the RSP drops the low bits of the
  fraction-by-fraction products, which can differ in the last place.
- Lighting, texture generation and fog are computed in floating point, the way RT64
  computes them, not in the RSP's fixed point.
- Flat shading gives the triangle its first vertex's colour.
- The modelview stack is taken as sixteen deep, the size of the `dram_stack` OoT gives
  the task, and display list calls as eighteen.

Not supported yet, each reported as a diagnostic: `G_MODIFYVTX` of a screen z,
`G_MOVEMEM` of the modelview or projection directly, and a task that loads another
microcode. Point lights are not read. `G_CULLDL` never culls, which changes no pixel.
Clip ratio and perspective normalisation are ignored, since the GPU clips in floating
point.

## Texture memory

`n64::TextureMemory` is the RDP's 4 KB of texture memory and its eight tile descriptors.
It takes the RDP commands that set and load them, in order, and loads byte for byte the
way RT64 does, which follows the hardware: `G_LOADTILE` and `G_LOADBLOCK` swap the 32-bit
halves of every odd row (for a block load, every row its `dxt` counter counts), 32-bit
RGBA puts red and green in the lower half of the memory and blue and alpha in the upper,
and `G_LOADTLUT` writes each palette entry four times into one 64-bit word of the upper
half.

Reading is three free functions, kept apart so the GPU's shader can be tested against
each: `findTileTexel` applies a tile's shift and corner to a coordinate, `wrapTexel`
clamps, mirrors and masks (an axis with no mask clamps), and `fetchTexel` decodes one
texel, through the palette when the other mode turns it on. Texture memory is meant to
reach the GPU as it is, and be read there the same way.

Format and size pairs the hardware has no decoder for (8-bit RGBA, 16-bit intensity,
32-bit intensity and the like) read as RT64 observed the hardware reading them. YUV is
not decoded.

## Combiner, blender and the RDP pass

`makeCombineMode` decodes `G_SETCOMBINE` into one name per input, `CombinerInput`, so a
selector no longer means different things in different slots; that table is what the
shader will read. `OtherMode` decodes the other mode's fields, the blender's among them.

`evaluateCombiner` and `evaluateBlender` are the reference the shader is tested against,
written in the RDP's own arithmetic as parallel-rdp reproduces it: the combiner's 9-bit
signed adders, so a sum past 255 clamps and a sum far enough past it wraps to black; the
second cycle reading the first's unclamped result and its texels swapped; one-cycle mode
combining with cycle 1 and blending with cycle 0; blender weights cut to five bits and a
result that wraps rather than saturates.

What they leave out, because it needs per-pixel state the CPU stages do not have:

- Coverage. Every pixel is taken as fully covered, so the final blender cycle blends only
  under `FORCE_BL`, which is how the interior of an opaque anti-aliased surface behaves,
  and colour-on-coverage never applies. Alpha-to-coverage takes the coverage it is given.
- Dither, for colour and for alpha. The noise input and the level-of-detail fraction are
  taken as given; the GPU stage computes them.

`runRdp` runs what the RSP produced through the RDP's state in order and emits every
triangle, texture rectangle and fill rectangle with the state it is drawn under: other
mode, combine mode, colour registers, key and convert constants, scissor, tile
descriptors, and a copy of texture memory. Primitives that share a state share one entry,
and texture memory is copied only when a load changed it.

## The GPU stage

`gfx::makeDrawBatches` turns `runRdp`'s output into vertices and draw calls. Triangles
keep their clip-space positions; texture and fill rectangles become two triangles in
screen pixels. Each draw carries the RDP state it needs as uniforms — combiner selectors,
colour registers, key and convert constants, other mode flags, the two tiles it samples,
and where its texture memory starts — and consecutive primitives with the same uniforms,
pipeline and scissor share a call. Every texture memory copy the frame needs goes up as one
storage buffer.

The vertex shader applies the viewport the RSP recorded with each triangle in clip space,
`x * scale + translate * w`, so the GPU still clips and still interpolates with
perspective. Depth is the N64's screen depth over its 0..1023 range; primitive depth
replaces it when the other mode says so.

The fragment shader repeats `n64/` in the same integer arithmetic: `findTileCoordinate`,
`wrapTexel` and `fetchTexel` against the raw texture memory, the RDP's three-point and
average filters, `evaluateCombiner`, the alpha compare, and the blender's first cycle and
any final cycle that reads no memory. A final cycle that reads memory is one of three
fixed-function blends (`gfx::makeBlendSetup`); anything else is drawn unblended and
reported.

Pipelines differ only in blend and depth state, and are created on first use. The device
settles the depth format once, so render targets and pipelines cannot disagree on it.
The renderer keeps its vertex, texture memory and upload buffers between frames, growing
them when a frame needs more, and submits without waiting; reading a target back waits.

Fill colours follow the colour image's pixel size: one RGBA5551 pixel of the register for
a 16-bit image, the whole register as RGBA8888 for a 32-bit one. In copy mode the alpha
compare drops a texel whose alpha bit is clear, and only for a 16-bit image, as
parallel-rdp does.

`toki_gpu_tests` renders small hand-written display lists and compares pixels with the CPU
reference. On D3D12 and on Vulkan the combiner matches `evaluateCombiner` exactly; texture
rectangles match `sampleTexture` pixel for pixel across point, bilinear and average
filtering, RGBA16 and 32, IA8, IA16 through a tile load's odd rows, and a mirrored,
wrapped 4-bit paletted texture; the shader-side blender matches `evaluateBlender`; and
translucency over memory is within three steps, as expected of the GPU's blending against
the RDP's five-bit weights.

Not reproduced yet:

- Level of detail. The LOD fraction is zero, texel 1 always comes from the next tile, and
  mipmaps are not selected.
- Texture conversion (`G_TC_CONV`) and YUV. Every texel is filtered, never converted.
- Copy mode decodes texels by their format rather than copying their raw bits into the
  framebuffer, which differs for formats other than RGBA16.
- Dither, coverage and anti-aliased edges, as on the CPU side.
- With texture perspective off, coordinates are halved, as RT64 does to match hardware;
  not yet checked against a reference.
- Rectangle texture coordinates carry a 256th-of-a-texel bias, so interpolation cannot
  floor a whole texel into the one before. Temporary, until rectangles step coordinates
  the way the RDP does.
- A fill rectangle into the depth buffer is skipped; the target's depth is cleared before
  every frame instead.

## Scope

In: one and two cycle combining, blender modes (fog, translucency, decal, alpha compare),
texture memory with wrap, mirror, clamp and masks, three-point filtering, fixed-point
transform and lighting, depth modes, primitive and environment colours, scissor.

Later: pixel-exact coverage and dithering, which need compute; framebuffer effects that
read back what was just drawn.

Not supported: F3DEX3. A checkout whose configuration enables `ENABLE_F3DEX3` is told so.

## Textures

Texture data reaches the renderer as N64 bytes, so the PNG-to-N64 encoding of package 8 is
what feeds it. Decoding those bytes is the renderer's job, in texture memory emulation.
