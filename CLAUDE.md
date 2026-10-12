# Toki64

A desktop workbench for modding The Legend of Zelda: Ocarina of Time on top of the
HackerOoT decompilation.

**HackerOoT:** https://github.com/HackerN64/HackerOoT

This is a C++ rewrite of an earlier Rust prototype (`../Toki64`). The prototype is a
reference for *what problems were already solved*, never for how to structure or style
this codebase.

The port is broken down in [WORK_PACKAGES.md](WORK_PACKAGES.md).

---

## Why this exists

Decomp modding needs many tools and many hacks, and the feedback loop is measured in
minutes: hand-edit C, run `make` under WSL, wait, boot an emulator, walk to the thing
you changed.

Specific gaps this tool closes:

- **fast64 in Blender draws every actor as a cube.** It cannot know which object and
  skeleton an actor uses, because that lives in the actor's C code.
- **Blender's UI is wrong for this.** Cluttered with everything irrelevant to N64/OoT.
- **No pipeline at all for non-geometry assets.** Dialogue text is a hand-edited format
  file. Music and sound have no tooling.
- **Many display lists are built in code** and only ever appear at runtime.

Nothing is an IDE for decomp. This is trying to be that.

## What it does

Reads the HackerOoT source tree and renders, inspects, and edits its assets without a
build. Generates the boilerplate the decomp requires. Drives the build.

## Non-goals

- **No ROM parsing.** The decomp source tree is the only input.
- **No geometry editing** for now. fast64 stays the authoring tool; this is a viewer.
  Editing is far back on the backlog.
- **No embedded emulator** for now. Architecture must not exclude it later.
- **Not multi-game.** OoT only. `z64` naming keeps the door open, nothing more.

---

## Architecture

### The central decision: a memory image, not a command tree

The old prototype parsed C into a typed `GfxCommand` tree and fed a hand-written
renderer. This one parses C into a **memory image with N64 segment addressing**, then
hands a segment table and a display list pointer to an off-the-shelf renderer.

Why:

- Every real N64 renderer wants pointers into memory, not a decoded tree. A decoded tree
  locks us into hand-rolling the renderer forever.
- It is what the game does (`gSPSegment(POLY_OPA_DISP, 0x03, roomSegment)`). Fidelity is
  a side effect.
- It is the prerequisite for ever running actor code to resolve display lists built at
  runtime. Not building that now; not excluding it either.

Segments are **per-segment buffers**, not one flat fake RDRAM. The game addresses by
segment, a contiguous 8 MB is never needed, and per-segment gives free bounds checks and
cheap reload of a single room.

### Rendering: our own N64 renderer

The renderer is written here, not vendored. It reads F3DEX2 display lists straight out of
the memory image, the way the N64 does, and is split where the hardware splits:

- **`n64/`** is the RSP running F3DEX2 and the RDP's state: matrices in the N64's fixed
  point, vertex transform, lighting, culling, segments, texture memory loads and tile
  descriptors, combiner and blender settings. Its output is a list of triangles in clip
  space, each carrying the full RDP state it is drawn with. It is plain CPU code, tested
  like a parser.
- **`gfx/`** draws that list through SDL3's GPU API (D3D12, Vulkan, Metal), clipping
  included. One fixed ubershader reads combiner and blender settings as data, so no shader
  is ever generated or compiled at runtime. Blender cycles that read the framebuffer map
  onto the GPU's own blend modes; a mode with no equivalent is a diagnostic.

Accuracy is the goal and F3DEX2 is the scope. F3DEX3 is not supported: a checkout that
enables it is told so, not drawn wrong.

Nothing off the shelf fits ([docs/renderer.md](docs/renderer.md) has the comparison).
Fast3D reads 64-bit PC display lists and approximates the combiner and blender; RT64 is
accurate but an emulator plugin with its own ImGui, backends and sixteen dependencies;
parallel-rdp is Vulkan-only and needs an emulated RSP. They are references instead.

### Layout

```
src/
  base/        What every module shares: the width aliases and Diagnostic.
  cparser/     A C front end: lexer, preprocessor, macro expansion, constant
               expressions, declarations, X-macro tables. Knows nothing of Zelda.
  makeparser/  A GNU make reader: variables, conditionals, includes, functions.
  z64/         Zelda64/N64 formats and decomp conventions. No processes, no UI.
  hackeroot/   The user's checkout: discovery, tables, config, build runner.
  n64/         The hardware: F3DEX2 on the RSP, RDP state, texture memory. CPU only.
  gfx/         The GPU side of the renderer: device, upload, ubershader, render targets.
  app/
    core/      ui/ = window, docking, theme, log panel   domain/ = settings
    assets/    ui/ = asset browser   domain/ = asset reports, scene and object names, background reads
    build/     ui/ = build panel   domain/ = process, output decoding, build runner
    viewport/  ui/ = viewport panel   domain/ = orbit camera, frame display list, reference grid
    displayList/ ui/ = display list panel   domain/ = a traced frame read into commands, names and RDP state
    actor/     ui/ = actor database panel   domain/ = the actor database, deriving entries from an overlay's C
    scene/  object/  texture/  message/  audio/          each: ui/ + domain/
    tools/
      createActor/   registerScene/   configure/         each: ui/ + domain/
  Main.cpp
```

`cparser/` is the machinery every asset domain reads C with: a lexer, a preprocessor with
include following and real conditional evaluation, a layered macro table and header
scrape, macro expansion, a constant expression evaluator, a declaration parser and an
X-macro table reader. It is general C. What is particular to a codebase is handed to it
by its caller: the `Dialect` its constant expressions are written in (cast types at the
target's widths, and builtins for macros it cannot expand), and a `MissingInclude` rule
for includes the build generates rather than finds.

`makeparser/` is general GNU make, for the same reason: reading a Makefile is not a
HackerOoT concern, only what is read out of HackerOoT's is.

`z64/` holds what is specific to the game and the decomp: the decomp's dialect
(`getDecompDialect`: the N64's type widths, the `u8`..`f64` typedefs, `TEX_LEN` and
`ARRAY_COUNT`), its rule for generated texture includes (`resolveTextureInclude`), texture
encoding as the build does it (`TextureEncoder`), gbi word emission
(`DisplayListEncoder`), type-directed data encoding (`DataEncoder`), the memory image and
its layout (`MemoryImage`), and the game code's data assets point at (`getCodeSymbols`). It
holds no domain structs.

`n64/` is the hardware, and what is true of every N64 game: segment addressing, the
physical address space (`Memory`), fixed-point matrices, and F3DEX2 running on the RSP
(`runTask`), and the RDP: its texture memory (`TextureMemory`, `fetchTexel`), its combiner
and blender (`evaluateCombiner`, `evaluateBlender`), and the pass that pairs every primitive
with the state it is drawn under (`runRdp`).

`cparser/` reads files, because a preprocessor that follows includes has to, but only in
three places: `SourceSet` opens files, `IncludeResolver` lists directories, and `Scrape`
walks a header tree. Everything else gets its bytes from a `SourceSet`. That keeps the
rule about `\\wsl.localhost` enforceable: filesystem cost lives in three classes, and a
new caller of `std::filesystem` in `cparser/` is a design question, not a convenience.

`hackeroot/` is deliberately named after the project it couples to, because everything in
it is HackerOoT-specific — the config header system, the table conventions, the Makefile
targets. Supporting vanilla decomp or MM later means a sibling module, not a generalized
one.

Asset parsing lives with its feature (`app/scene/domain/`, `app/object/domain/`), not in a
shared parser module.

### Dependency rules (hard)

- `base/` depends on nothing else in `src/`.
- `cparser/` and `makeparser/` depend on `base/` only, and never on each other.
- `z64/` may depend on `base/`, `cparser/` and `n64/`.
- `hackeroot/` may depend on `base/`, `cparser/`, `makeparser/` and `z64/`.
- `n64/` depends on `base/` only. It is the hardware, and knows nothing of Zelda.
- `gfx/` may depend on `base/`, `n64/` and `z64/`.
- `app/**/domain/` may depend on everything above.
- `app/**/ui/` may depend on anything.
- **`domain/` must never include `ui/`.** This is what keeps parsers testable without SDL
  or ImGui, and it is not negotiable.
- Nothing outside `app/` depends on `app/`.

### Build targets

- `toki_core` — OBJECT library: `base/`, `cparser/`, `makeparser/`, `n64/`, `z64/`,
  `hackeroot/`, and every `app/**/domain/**`.
- `toki_gfx` — static library: `gfx/` and the ubershader, compiled at build time from
  `gfx/shaders/Ubershader.hlsl` by `shadercross` into the formats the platform's backends
  read (DXIL and SPIR-V on Windows, MSL and SPIR-V on Apple, SPIR-V elsewhere) and
  embedded.
- `toki` — executable: `Main.cpp` plus every `app/**/ui/**`.
- `toki_tests` — links `toki_core` only.
- `toki_gpu_tests` — links `toki_core` and `toki_gfx`, draws on a real GPU, and skips every
  test when there is none.

`gfx/` is kept out of `toki_core` on purpose. An object library propagates its usage
requirements, so folding it in would drag a GPU device into the test binary and break the
rule that tests need no GPU. Everything in the renderer that can be tested without one lives
in `n64/`.

Source lists are **explicit**. No globbing — directory shape must not silently determine
the build graph.

---

## Tech stack

| Layer | Choice |
|---|---|
| Language | C++23 |
| Build | CMake + vcpkg manifest mode + Ninja + CMakePresets |
| Renderer | Our own: F3DEX2 in `n64/`, GPU side in `gfx/` |
| GFX backend | SDL3 GPU (D3D12, Vulkan, Metal) |
| Shaders | HLSL, compiled by SDL_shadercross at build time |
| Window/input | SDL3 |
| UI | Dear ImGui (docking) + ImGuizmo |
| Math | glm |
| Tests | Catch2 |

Dear ImGui's defaults read as a debug tool. Custom theme, font stack, icon font and a
consistent spacing scale are a budgeted requirement, not polish.

Every form is two columns: the label on the left, its input filling the right
(`core::beginProperties`, `core::drawPropertyLabel`). Never ImGui's default of the input
with its label after it. A toggle is a row like any other, its label and then the checkbox
alone.

---

## Concepts

Each part of the system with more to it than fits here is written up in full under
[docs/](docs/README.md). Read the document before changing the code it describes. What is
listed below is only what no change may break.

- **[Reading C](docs/c-parsing.md)** — how `cparser` reads decomp C without a build: a
  lazy preprocessor over a targeted parser, layered macro tables, include resolution,
  embedded files.
  - `gs*` macros are never expanded. Which macros to expand is the consumer's decision.
  - Reading a file twice reads it the same way: a unit never writes to the macros it is
    read against.
  - Nothing in the parsing layer calls `exists` in a loop. Filesystem questions go through
    `DirectoryCache`.
  - An unknown macro or a missing include is a diagnostic naming it, never a silent skip
    and never a failure of the whole read.
- **[Build configuration](docs/build-configuration.md)** — why an `#if` must be answered
  the way the build answers it, and how the Makefile and `config.h` become a macro layer.
  - An `#if` in an ordinal table is never read quietly: a wrong answer shifts every ID
    after it.
  - Nothing runs make, or any other process, to learn the configuration.
- **[gbi words](docs/gbi.md)** — where gbi constants and the words of `gs*` macros come
  from.
- **[The memory image](docs/memory-image.md)** — how asset C becomes per-segment buffers,
  byte for byte as the build compiles it.
  - An object's bytes are checked against the build's own object files. "Close" is a bug.
  - An address is a relocation with its addend in place, so data is encoded before it is
    laid out, and arithmetic an address cannot survive is a diagnostic.
  - Files that must match the build read their system headers as a prelude, under the
    configuration alone and expanded as read, not against the scrape.
- **[The workspace](docs/workspace.md)** — how one checkout is read, kept current off the
  UI thread, and watched for changes.
  - Nothing on the UI thread reads the checkout. A read takes seconds over
    `\\wsl.localhost`.
  - Change detection polls modification times. Windows sends no change notifications for
    the WSL share, so a watcher built on them would silently never fire.
- **[Scenes](docs/scenes.md)** — how a scene folder becomes images, how its headers are read
  out of them, and how it is drawn and reloaded.
  - Headers are read out of the memory image's bytes, as `Scene_ExecuteCommands` reads them,
    never out of the C: the bytes are what the game sees.
  - What the game draws a room with comes from the checkout's own code (`sSetupDL`,
    `sDefaultDisplayList`), not from constants written down here.
- **[Objects](docs/objects.md)** — how an object folder becomes a catalog of assets, and how
  a skeleton is posed and drawn.
  - An object is a catalog, not a model. What is drawn is picked from it; nothing assumes
    one skeleton per object.
  - Asset kinds come from the declared type of each symbol, not from guessing at bytes.
- **[Textures](docs/textures.md)** — how textures are found in an image, paired with their
  palettes and decoded.
  - A texture's format, size and palette come from the file its include resolved to, as the
    build reads the name, recorded on the image symbol. Never guessed from a symbol name.
  - What is shown is decoded from the image's bytes, not read from the PNG.
- **[The actor database](docs/actor-database.md)** — how each actor is shown, by params
  given as masks and variants of parts matched on them, kept per checkout in
  `.toki/actors.toml`, seeded from Toki's own.
  - The checkout's database is the source of truth. An actor with an entry is drawn as it
    says, and one without is a marker; its C is read only when an entry is derived.
  - An entry is derived only when a person asks, or for an actor without one. Nothing
    overwrites an entry otherwise.
  - It changes how Toki shows actors, never the checkout's code.
- **[Actor rendering](docs/actor-rendering.md)** — how a guess is read from the literals in
  an overlay's `Init` and `Draw`, and how placed actors are drawn as `Actor_Draw` draws them.
  - Only literals count in a guess. What is not one is a gap with its reason and location.
  - Actor code is never run.
- **[Tools](docs/tools.md)** — how Configure, Register Scene and Create Actor change the
  checkout's code.
  - Every edit is a plan shown before it is written, and a tool run twice writes nothing
    the second time.
  - Nothing is written over a file that changed since the plan read it.
  - What a tool adds goes into a `// Toki64 <what> — managed block`.
- **[The display list viewer](docs/display-list-viewer.md)** — how the commands the viewport's
  frame ran are shown, explained and tied back to what they drew.
  - What is shown is what the RSP ran, from its trace, never what the C says.
  - The viewport traces only while the panel is open.
- **[The renderer](docs/renderer.md)** — what was evaluated, what is taken from where, and
  how the RSP, RDP and GPU stages divide the work.
  - Everything that can be computed on the CPU is, in `n64/`, under tests. `gfx/` only
    draws.
  - Code taken from an MIT renderer keeps its notice in `THIRD_PARTY_NOTICES.md`. GPL
    renderers (GLideN64, mupen64plus) are read, never copied.

---

## Code style

Strict and clean. The prototype is not the reference here — notably it declared "no banner
comments" and then used them everywhere.

### Enforced by tooling

`.clang-format` and `.clang-tidy` are checked in and authoritative. If a rule below is
expressible in either, it lives there rather than in prose.

### Naming

- Types: `PascalCase`. Functions, variables, members: `camelCase`.
- Every function name contains a verb, because a call is an action:
  `getDecompDialect()`, not `decompDialect()`. Accessors are `get`-prefixed and mutators
  `set`-prefixed, so a member and its accessor are `windowTitle` and `getWindowTitle()`.
  Predicates read as questions (`isActive`, `hasIdentifier`, `contains`), functions that
  build a value are `make`-prefixed (`makeInteger`), and lookups that may come back empty
  are `find`-prefixed (`findType`). The rule covers free functions, members, callbacks
  stored in members (`findArrayLength`) and test helpers alike.
- No prefix and no trailing underscore on members.
- Constants and enumerators: `PascalCase`.
- Files: `PascalCase`, named after the primary type they declare (`LogPanel.h`,
  `Types.h`). Directories: lowercase, `camelCase` when multiword (`createActor/`).
- Name a variable after its type unless a better name exists: a `SetupWizard` member is
  `setupWizard`, not `wizard`. The type is the clearest thing about it.
- Namespaces: `base::`, `cparser::`, `makeparser::`, `z64::`, `hackeroot::`, `gfx::`, and
  `toki::<feature>` for anything under
  `app/` (`toki::core`, `toki::scene`, `toki::createActor`). The `ui/` and `domain/`
  split is a directory boundary, not a namespace level — nesting it would make every name
  four levels deep for no gain.
- Full words. Abbreviate only where universally obvious in this domain: `z64`, `gfx`,
  `dl`, `idx`, `len`.
- The width aliases in `base/Types.h` (`u8`, `u32`, `s16`, `f32`, `usize`, ...) are the one
  exception to `PascalCase` types. They are spelled the way the decomp and the gbi headers
  spell them so parsed structs read next to the C they came from.

### Types and ownership

- `unique_ptr` owns. `shared_ptr` requires a written justification at the declaration.
- Raw pointers and references are always non-owning.
- `const` by default, on locals, parameters and methods.
- `[[nodiscard]]` on anything returning a value the caller must not drop.
- `auto` only when the type is spelled on the right-hand side or is unutterable.
- `struct` for passive aggregates, `class` when there is an invariant to protect.
- Integers use the width aliases, not `std::uint32_t`: `u8`/`u16`/`u32`/`u64`,
  `s8`/`s16`/`s32`/`s64`, `f32`/`f64`. Sizes and indices use `usize` instead — a count of
  host elements is not N64 data, and `u32` there truncates on 64-bit and warns at every
  comparison against `.size()`.
- The aliases live in `namespace base` rather than at global scope, because any libultra
  header brings in typedefs of the same names, and its `u64` is a different type from
  `std::uint64_t` on LP64. Every other namespace gets them from its
  own `Types.h` (`z64/Types.h`, `cparser/Types.h`, `app/<feature>/Types.h`, ...), which is
  a block of `using base::u32;` declarations and nothing else. A new namespace gets one too.
- No out-parameters. Return a value or a struct.

### Errors

- `std::expected<T, Error>` at every fallible boundary.
- **Do not throw.** Third-party code may; ours does not.
- User-facing failures carry enough context to act on — a file path, a symbol name, a
  line number. Never a bare "parse failed".

### Headers

- `#pragma once`.
- Include what you use. Forward declare to keep headers light.
- Never `using namespace` at namespace scope in a header.
- One primary type per header.

### Comments

Two kinds of comment are allowed, and nothing else:

- Function documentation on central infrastructure — the types and free functions the
  rest of the codebase is built on top of.
- A note on a temporary workaround or a hack, saying what makes it temporary.

Everything else goes uncommented. Readable code makes *what* self-evident, and a
running commentary on *why* ages into a lie. No banner comments, no section dividers,
no commented-out code.

### Size

A file past ~400 lines or a function past ~50 is a signal to look for a split, not a hard
limit. The prototype had 1200-line files; that is what we are avoiding.

### Tests

Every parser and every table reader gets tests. They link the object library and must
never require SDL, ImGui or a GPU.

What only a GPU can check — that the ubershader computes what `n64/` computes — lives in
`toki_gpu_tests`, which compares rendered pixels against the CPU reference functions.
Changing either side of that pair means running both test binaries.

---

## Workflow

- **Never commit automatically.** Commit only when explicitly asked.

---

## Open

- Exact `.clang-format` base and column limit.
- Whether a cold read needs parallel file reads. A vanilla scene is 0.6 s cold and 0.09 s
  warm over `\\wsl.localhost`, all of it in ~100 serial file opens.
- Whether the header scrape needs an on-disk cache keyed by mtime. It is 1.4 s for 263
  headers and 7,496 defines, paid once per checkout per session.
- MM support. Deliberately unplanned.
