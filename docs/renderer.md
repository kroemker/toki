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

Stages 1 and 2 are in `toki_core` and are tested the way parsers are. Only the shader's
arithmetic needs a pixel comparison, against RT64 or an emulator screenshot until a
parallel-rdp harness exists.

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
