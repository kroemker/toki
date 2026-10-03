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
  base/        What every module shares: the width aliases and Diagnostic.
  cparser/     A C front end: lexer, preprocessor, macro expansion, constant
               expressions, declarations, X-macro tables. Knows nothing of Zelda.
  makeparser/  A GNU make reader: variables, conditionals, includes, functions.
  z64/         Zelda64/N64 formats and decomp conventions. No processes, no UI.
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

`cparser/` is the machinery every asset domain reads C with: a lexer, a preprocessor with
include following and real conditional evaluation, a layered macro table and header
scrape, macro expansion, a constant expression evaluator, a declaration parser and an
X-macro table reader. It is general C.

`makeparser/` is general GNU make, for the same reason: reading a Makefile is not a
HackerOoT concern, only what is read out of HackerOoT's is.

`z64/` holds what is specific to the game and the decomp: segment addressing, the N64
target and the decomp's conventions, and later gbi word emission, the memory image and
texture format decoding. It holds no domain structs.

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
- `z64/` may depend on `base/` and `cparser/`.
- `hackeroot/` may depend on `base/`, `cparser/`, `makeparser/` and `z64/`.
- `gfx/` may depend on `base/` and `z64/`.
- `app/**/domain/` may depend on everything above.
- `app/**/ui/` may depend on anything.
- **`domain/` must never include `ui/`.** This is what keeps parsers testable without SDL
  or ImGui, and it is not negotiable.
- Nothing outside `app/` depends on `app/`.

### Build targets

- `toki_core` — OBJECT library: `base/`, `cparser/`, `makeparser/`, `z64/`, `hackeroot/`,
  and every `app/**/domain/**`.
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

**A real preprocessor, but a lazy one, over a targeted parser.** The prototype expanded
all of `gbi.h`/`z64.h` up front to learn what `gsSPVertex` meant; that was the source of
its multi-second load times. The fix is not to skip expansion — real asset C cannot be
read without it — but to expand only what a consumer asks for, and to take macro
*definitions* from a cheap scrape instead of from resolving the include graph.

What the checkout actually contains decided this. fast64 emits `#if
ENABLE_ANIMATED_MATERIALS` inside a `SceneCmd[]`, writes rotations as
`DEG_TO_BINANG(0.000)`, and sizes arrays with a `#define LENGTH_…` from a sibling header.
Extracted vanilla assets are a different shape again:

```c
u64 spot00_scene_00013D98_Tex[TEX_LEN(u64, spot00_scene_00013D98_Tex_WIDTH, …, 16)] = {
#include "assets/scenes/overworld/spot00/spot00_scene_00013D98_Tex.rgba16.inc.c"
};
```

That include is spelled from the repository root, sits inside an initializer, and names a
file that does not exist — the build generates it from a `.png`.

The pieces:

1. **A lexer**, `Scanner`. Comments and line continuations are trivia; `#` comes back as
   an ordinary punctuator carrying a line-start flag. A token points at the `SourceFile`
   it came from and its text is a `string_view` into that file's contents, so whatever
   owns the file must outlive the token.
2. **`MacroTable`, filled by a define-only scrape** of the checkout's header tree
   (`Scrape`). One linear scan per header, conditionals ignored, first definition of a
   name wins. This is how `SCENE_CMD_ROOM_LIST`, `BLEND_RATE_AND_FOG_NEAR` and the
   `config/` feature flags become known without resolving a single include, and it tracks
   decomp changes on its own rather than going stale. Both arms of an `#if` are seen; that
   is the price, and it is cheaper than the alternative.
3. **`Preprocessor`** — directives, real `#if`/`#elif`/`#ifdef` evaluation, `#define`, and
   include splicing into one token stream. Macro uses are *left standing*: which of them
   to expand is the consumer's decision, and `gs*` must never be expanded at all.

**Ownership is layered, and nothing reads into shared mutable state.** The scrape's
`ScrapeResult` owns the header files and the base `MacroTable`. Each `preprocess` call
returns a `TranslationUnit` that owns its own `SourceSet` and a `MacroTable` layered over
that base: its `#define`s, `#undef`s and include guards live in the layer and never reach
the base, so reading a file twice reads it the same way and one scene cannot leak into
the next. A reload is a new unit; the old one stays valid until dropped. The only
lifetime rule is that the base outlives, and does not move under, every unit read
against it. Layers stack, which is where build-derived predefines and a properly
preprocessed `config/` belong.
4. **`Expand`** — on-demand macro expansion of a token range, with a stop-list. Every
   token carries a hide set, as the standard describes, and a replacement is rescanned
   together with the tokens after it, so `#define F G` then `F(1)` reaches `G(1)`.
   Variadic macros, both `...` and GNU's `args...`, are supported. `#` and `##` are
   diagnosed, not implemented.
5. **`Value` and `Expression`** — a constant expression evaluator over the C operators the
   decomp uses, including casts that truncate the way the named type does. Integer and
   floating point both, because `DEG_TO_BINANG` is `(s16)(s32)((degrees) * (0x8000 /
   180.0f))`. `ARRAY_COUNT` and `TEX_LEN` are builtins rather than expansions: their real
   definitions are written with `sizeof`, which nothing here can answer.
6. **`Parser`** — file-scope declarations only, as `{name, type, arrayLength, initializer
   token span}`, one per declarator in a list. Typedefs, struct definitions, functions
   and function prototypes are stepped over; anything else it cannot read is a note. The
   initializer stays *unevaluated tokens*, because the memory image and the scene reader
   want different things out of it and neither should pay for the other's reading.

### gbi constants and `gs*` words

**gbi constants come free.** The `gs*` macro set is a frozen 1996 SDK header. Vendored
Fast3D ships `gbi.h`, so `G_RM_AA_ZB_OPA_SURF`, `G_CC_MODULATEIA` and friends are real
values compiled into the binary. Resolve identifiers against a generated name-to-value
map; never parse decomp headers for them.

**The words come from the real macros.** `gsDPLoadTextureBlock` expands to seven commands,
not one, and a hand-written macro-to-words table covering gbi.h is both a lot of code and
a thing that goes stale. Instead a build-time generator scrapes `gbi.h` for macro names and
arity and emits a dispatcher that calls the actual dynamic `g*(pkt, …)` macro with the
evaluated arguments, letting the SDK header encode the words itself. Not built yet; it
belongs with the memory image.

### Include resolution

Two roots, tried in order: the including file's own directory, then each configured search
path — for HackerOoT the repository root and `extracted/<version>`, which is what the
Makefile passes to `cpp`. An include is followed only when it resolves *inside* an allowed
root, so a scene reaches its rooms and never descends into `z64.h`. A missing include is a
diagnostic naming it, never a failure. Cycles are broken on the active include stack, not
on a visited set: the same `.inc.c` is legitimately included by two different arrays.

**A `.inc.c` that does not exist is a texture.** `X.rgba16.inc.c` resolves to the
`X.rgba16.png` the build would have generated it from, reported as an `ImageReference`
carrying the format its name spells. It also stands in the token stream as an `Image`
token where the include was, so the declaration it fills holds it in its initializer. A paletted texture's image also carries the palette it
was extracted against, so the include's spelling is only a prefix of the filename. Never
parse a vanilla texture as C: the PNG is both faster and the only thing actually on disk.

**Every filesystem question is answered from a cached directory listing.** A checkout on
`\\wsl.localhost` charges milliseconds per `stat` and a vanilla scene asks hundreds:
answering them one at a time cost 3 s per scene, a listing per directory costs 0.6 s cold
and 0.09 s warm. Nothing in the parsing layer may call `exists` in a loop.

### `#if` policy

**Asset `.c` files need it.** fast64 emits `#if ENABLE_ANIMATED_MATERIALS` around a scene
command, and the answer lives in the checkout's own `include/config/*.h`.

**Ordinal table files need it most.** `SceneID` and `ActorID` come from position in an
X-macro table, so reading one conditionally-disabled entry shifts every subsequent ID by
one — silent, and it corrupts actor placement and scene registration. `readTable` therefore
preprocesses with `strictConditionals`, which raises an unresolved identifier in a
condition from a note to a warning. An `#if` in a table is never read quietly.

**Resolving a name is not the same as resolving it correctly.** The scrape knows every
flag, but only its first-seen arm, and the flags tables actually hinge on
(`DEBUG_FEATURES`, `OOT_VERSION`, `PLATFORM_*`) are not in any header at all: they are
`-D` arguments the Makefile builds. So conditions are answered against a **configuration
layer** between the scrape and every unit (`hackeroot/Configuration`):

- `readBuildDefines` reads the checkout's own Makefile with `makeparser`, a small GNU make
  evaluator —
  assignments of every flavour, conditionals, includes, `define`, the text functions —
  and takes `CPP_DEFINES` and `GBI_DEFINES` as make would expand them. Running make is
  not an option: parsing HackerOoT's Makefile alone runs `mkdir -p`, `find` over the tree
  and `cpp` over the spec. So nothing is executed; `$(shell)` expands to nothing and marks
  what it fed as uncertain, and only an uncertain *define* is reported. The one `$(shell)`
  whose output matters, `VERSION_MACRO`, is derived and passed as a command-line
  override, which is what make itself would do with it.
- `readConfiguration` turns those arguments into source text and preprocesses
  `include/config.h` under them, against an empty base (the scrape has seen `CONFIG_H`
  and would skip the file). The result becomes a layer over the scrape, and every name
  the config headers define in some arm but not in this configuration is hidden, so the
  scrape's first-seen arm can never answer for it.

The user's build settings (`VERSION`, `DEBUG_FEATURES`, ...) are make's command line,
and flow through exactly as they would in `make VERSION=... `.

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
- The aliases live in `namespace base` rather than at global scope, because vendored
  Fast3D's `gbi.h` brings in the libultra typedefs of the same names and its `u64` is a
  different type from `std::uint64_t` on LP64. Every other namespace gets them from its
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

---

## Workflow

- **Never commit automatically.** Commit only when explicitly asked.
- Vendored Fast3D changes stay minimal and documented, so upstream can be re-merged.

---

## Open

- Exact `.clang-format` base and column limit.
- Whether a cold read needs parallel file reads. A vanilla scene is 0.6 s cold and 0.09 s
  warm over `\\wsl.localhost`, all of it in ~100 serial file opens.
- Whether the header scrape needs an on-disk cache keyed by mtime. It is 1.4 s for 263
  headers and 7,496 defines, paid once per checkout per session.
- OpenGL vs D3D11 as the shipping backend on Windows.
- MM support. Deliberately unplanned.
