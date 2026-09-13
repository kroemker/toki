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

### Rendering: vendored Fast3D

Fast3D (MIT) lives inside `libultraship/src/graphic/Fast3D/`. It is extracted and
vendored into `third_party/Fast3D/`, not consumed via libultraship — LUS drags in the OTR
resource system plus its own window, audio, input and ImGui stacks. Expect a maintained
diff against upstream.

Fast3D backends are OpenGL, D3D11, D3D12, Metal. **There is no Vulkan backend**, which is
why parallel-rdp is not used: it is Vulkan-only, and LLE would additionally require an RSP
running F3DZEX2 microcode against real RDRAM. Revisit only if an emulator core is ever
embedded.

### Layout

```
src/
  z64/         Pure Zelda64/N64 formats. No filesystem, no processes, no UI.
  hackeroot/   The user's checkout: discovery, tables, config, build runner.
  gfx/         Fast3D binding: segment upload, viewport target, camera.
  app/
    core/      ui/ = window, docking, theme, log panel   domain/ = settings
    scene/  object/  texture/  message/  audio/          each: ui/ + domain/
    tools/
      createActor/   registerScene/   configure/         each: ui/ + domain/
  Main.cpp
third_party/Fast3D/
```

`z64/` exposes the machinery every asset domain needs: a C token scanner, a constant
expression evaluator, an X-macro table reader, gbi word emission, the memory image and
segment addressing, and texture format decoding. It holds no domain structs.

`hackeroot/` is deliberately named after the project it couples to, because everything in
it is HackerOoT-specific — the config header system, the table conventions, the Makefile
targets. Supporting vanilla decomp or MM later means a sibling module, not a generalized
one.

Asset parsing lives with its feature (`app/scene/domain/`, `app/object/domain/`), not in a
shared parser module.

### Dependency rules (hard)

- `z64/` depends on nothing else in `src/`.
- `hackeroot/` and `gfx/` may depend on `z64/`, and on nothing else in `src/`.
- `app/**/domain/` may depend on `z64/`, `hackeroot/`, `gfx/`.
- `app/**/ui/` may depend on anything.
- **`domain/` must never include `ui/`.** This is what keeps parsers testable without SDL
  or ImGui, and it is not negotiable.
- Nothing outside `app/` depends on `app/`.

### Build targets

- `toki_core` — OBJECT library: `z64/`, `hackeroot/`, and every `app/**/domain/**`.
- `toki_gfx` — static library: `gfx/`, plus Fast3D and the GL backend. Does not exist yet.
- `toki` — executable: `Main.cpp` plus every `app/**/ui/**`.
- `toki_tests` — links `toki_core` only.

`gfx/` is kept out of `toki_core` on purpose. An object library propagates its usage
requirements, so folding it in would drag a GL context into the test binary and break the
rule that tests need no GPU.

Source lists are **explicit**. No globbing — directory shape must not silently determine
the build graph.

---

## Tech stack

| Layer | Choice |
|---|---|
| Language | C++23 |
| Build | CMake + vcpkg manifest mode + Ninja + CMakePresets |
| Renderer | Fast3D, vendored |
| GFX backend | OpenGL 3.3 first; D3D11 available |
| Window/input | SDL3 |
| UI | Dear ImGui (docking) + ImGuizmo |
| Math | glm |
| Tests | Catch2 |

Dear ImGui's defaults read as a debug tool. Custom theme, font stack, icon font and a
consistent spacing scale are a budgeted requirement, not polish.

---

## Parsing strategy

**Targeted parsers. No general C preprocessor, no macro expansion.** The prototype
expanded all of `gbi.h`/`z64.h` up front to learn what `gsSPVertex` meant; that was the
source of its multi-second load times.

Because the target is a memory image, a `gs*` macro only has to become **two 32-bit
words** — no semantic model of each command is needed.

Four pieces, none of them a preprocessor:

1. **gbi constants come free.** The `gs*` macro set is a frozen 1996 SDK header. Vendored
   Fast3D ships `gbi.h`, so `G_RM_AA_ZB_OPA_SURF`, `G_CC_MODULATEIA` and friends are real
   values compiled into the binary. Resolve identifiers against a generated name-to-value
   map; never parse decomp headers for them.
2. **A small constant expression evaluator** — integer literals, `|`, `<<`, negation.
3. **Scoped include following.** Follow `#include` only when it resolves inside the asset
   directory (scene to its room files). Never descend into `z64.h` or `ultra64.h`.
4. **Preprocessor conditionals are recognized but not evaluated.** See below.

### `#if` policy

Asset `.c` files do not need it — fast64 emits clean data, and extracted vanilla asset C
is plain.

**Ordinal table files do.** `SceneID` and `ActorID` come from position in an X-macro
table, so reading one conditionally-disabled entry shifts every subsequent ID by one —
silent, and it corrupts actor placement and scene registration.

So: the scanner recognizes `#if`/`#ifdef`/`#else`/`#endif` as directives and **reports**
them. An unhandled conditional in a table file is a loud warning, never a silent skip.
Evaluation against scraped config defines gets built when a real table forces it.

### Unknown macros

HackerOoT adds its own `gs*` macros and users will add more. An unknown macro must
produce a diagnostic naming it exactly, and the macro table must be user-extensible.
Never fail silently.

---

## Code style

Strict and clean. The prototype is not the reference here — notably it declared "no banner
comments" and then used them everywhere.

### Enforced by tooling

`.clang-format` and `.clang-tidy` are checked in and authoritative. If a rule below is
expressible in either, it lives there rather than in prose.

### Naming

- Types: `PascalCase`. Functions, variables, members: `camelCase`.
- No prefix and no trailing underscore on members. Where a member would collide with its
  accessor, the accessor is `get`-prefixed: `getWindowTitle()` returns `windowTitle`.
  Mutators are `set`-prefixed.
- Constants and enumerators: `PascalCase`.
- Files: `PascalCase`, named after the primary type they declare (`LogPanel.h`,
  `Types.h`). Directories: lowercase, `camelCase` when multiword (`createActor/`).
- Name a variable after its type unless a better name exists: a `SetupWizard` member is
  `setupWizard`, not `wizard`. The type is the clearest thing about it.
- Namespaces: `z64::`, `hackeroot::`, `gfx::`, and `toki::<feature>` for anything under
  `app/` (`toki::core`, `toki::scene`, `toki::createActor`). The `ui/` and `domain/`
  split is a directory boundary, not a namespace level — nesting it would make every name
  four levels deep for no gain.
- Full words. Abbreviate only where universally obvious in this domain: `z64`, `gfx`,
  `dl`, `idx`, `len`.
- The width aliases in `z64/Types.h` (`u8`, `u32`, `s16`, `f32`, `usize`, ...) are the one
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
- The aliases live in `namespace z64` rather than at global scope, because vendored
  Fast3D's `gbi.h` brings in the libultra typedefs of the same names and its `u64` is a
  different type from `std::uint64_t` on LP64. Every other namespace gets them from its
  own `Types.h` (`hackeroot/Types.h`, `gfx/Types.h`, `app/<feature>/Types.h`), which is a
  block of `using z64::u32;` declarations and nothing else. A new namespace gets one too.
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

---

## Workflow

- **Never commit automatically.** Commit only when explicitly asked.
- Vendored Fast3D changes stay minimal and documented, so upstream can be re-merged.

---

## Open

- Exact `.clang-format` base and column limit.
- Whether `#if` evaluation ever gets built, or the handful of real cases stay
  special-cased.
- OpenGL vs D3D11 as the shipping backend on Windows.
- MM support. Deliberately unplanned.
