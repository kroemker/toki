# Work packages

Porting the Rust prototype (`../Toki64`) to this codebase, in order.

The ordering puts the UI first on purpose. The shell, the workspace layer and the asset
browser are the same work no matter what ends up rendering. Doing them first also means
every parser package that follows can be exercised from the running app instead of from
unit tests alone. The one exception is the renderer, which sits right after the workspace:
it depends on nothing before it, and it fixes what package 8's memory image must hold.

## Standing rules

- **Each package is committed when it is done**, before the next one starts. A package that
  lands as one uncommitted pile with the next cannot be reviewed or reverted on its own.
- **Parsers and table readers get tests**, against fixtures and against a real checkout.
  `tests/hackeroot/CheckoutTest.cpp` runs when the build is configured with
  `-DTOKI_HACKEROOT=<path>` and skips otherwise. Every package that reads the checkout adds
  to it. Hand-written fixtures missed the two worst bugs found so far: a scene table read
  one entry short, and the include guard that made a reread skip a header.
- **One source of build settings.** `VERSION`, `DEBUG_FEATURES` and the rest are make's
  command line, persisted once. The workspace (package 4), which feeds them to the
  configuration layer, and the build runner (package 6) both take them from there. A viewer reading a debug configuration while the
  build produces release gets every scene ID wrong without a single warning.

---

## 1. App shell (done)

The window already exists; this makes it an application.

- Dockable panel system and layout persistence
- Settings: a persisted store, plus the HackerOoT path
- Setup wizard for first run when no checkout is configured
- Log panel with levels and filtering
- Menu bar
- Theme, font stack, icon font, spacing scale

Settle the visual design here. Retrofitting a theme later is worse than starting with one.

Done when the app restores its layout and settings across restarts and anything can log to
a panel.

Ports from `toki64/src/{app,settings,logger}.rs`, `ui/{tabs,log_panel,menu,setup_wizard}.rs`.

## 2. The C front end (done)

Wider than first planned, because the real checkout forced it. See
[docs/c-parsing.md](docs/c-parsing.md) for why; the short version is that fast64 emits
`#if` and function-like macros inside asset `.c`, extracted assets include texture data
from files the build generates from `.png`, and array bounds come from `#define`s in a
sibling header. None of that can be read without a preprocessor.

- `Scanner` — lexer over decomp C
- `SourceSet` — owns every file one reading pass opened, so tokens can be `string_view`s
- `MacroTable` + `Scrape` — define-only scrape of the checkout's header tree, as the base
  layer every translation unit's own macros sit on
- `Preprocessor` — directives, real `#if` evaluation, include splicing, `.inc.c` to `.png`
- `Expand` — on-demand macro expansion with a stop-list for `gs*`
- `Value` + `Expression` — constant expressions, integer and float, with C casts
- `Parser` — file-scope declarations with unevaluated initializer spans
- `Initializer` — splitting a brace body into elements
- `TableReader` — X-macro ordinal rows, preprocessed so `#if` in a table is answered

The ordinal behaviour was the point. An `#if` whose condition nothing resolves is a
warning naming the identifier, never a silent skip. But the first measurement, 102 rows
with `oceantest_scene` at 101, was wrong for the build it claimed to describe: it read
`DEBUG_FEATURES` as 0 because that flag is a Makefile `-D`, and so dropped
`example_scene`. The strict warning fired; the result was still wrong. That is what the
configuration layer below fixes.

**Configuration layer (done).** `hackeroot/` reads the Makefile's `CPP_DEFINES` and
`GBI_DEFINES` with a make evaluator that never runs anything, preprocesses
`include/config.h` under them, and layers the result between the scrape and every unit.
Against the same checkout (`VERSION ?= ntsc-1.2`, `DEBUG_FEATURES ?= 1`) the scene table
is 103 rows, `example_scene` at 101 and `oceantest_scene` at 102, and the scene, actor,
object and entrance tables all read with no warnings. Make evaluation is 95 ms for its two
passes, the configuration 38 ms.

Measured against `//wsl.localhost/Ubuntu/home/leo/HackerOoT`, Debug build: header scrape
1.4 s for 263 headers and 7,496 defines; `spot00_scene.c` 162k tokens, 51 images and 465
declarations in 0.6 s cold and 0.09 s warm; parsing itself under 10 ms throughout. The
cost is filesystem round trips, not lexing, which is why resolution goes through cached
directory listings.

Replaces `c-parser/*` and the front half of the prototype's parsing layer.

## 3. Front-end gaps (done)

Package 2 was done in the sense that it read the real checkout. These are the gaps a
review found afterwards, each confirmed with a probe and each now covered by a regression
test. `CheckoutTest` also preprocesses and parses the real `spot00_scene.c` and requires
no warnings from either.

**Blocking:**

- **Images are linked to the declarations they fill.** The preprocessor emits a
  `TokenKind::Image` token where a texture include was, carrying its index into
  `TranslationUnit::images`, so a declaration's initializer holds its image. A `[]` array
  whose element is an image has an unknown length rather than a counted one; its real
  length is the PNG's size times its bit depth, which package 8 reads. `CheckoutTest`
  checks that every image in the real `spot00_scene.c` sits in exactly one declaration,
  each sized by `TEX_LEN`. fast64 exports write texture data inline as `u64` literals,
  so this only matters for extracted assets.
- **`float` stays `float`.** `Value` carries `Integer`, `Float` or `Double`. A float
  literal, a `(f32)` cast and arithmetic between floats round to single precision, an
  integer meeting a float is converted to float first, and a double anywhere widens the
  operation, as in C. `DEG_TO_BINANG(210.13)` is −27283.

**Correctness:**

- `Expand` works on hide sets: a replacement rescans together with the tokens after it,
  `...` and GNU's `args...` collect into the last parameter, and `F()` is one empty
  argument. Its tests include the standard's own `f(2)(9)` example.
- `Parser` reads every declarator in a list, steps over function prototypes, strips
  `__attribute__`, falls back to counting the initializer when a bound names something
  unresolved, and notes what it cannot read (function pointers, unclosed declarations).
- `&&`, `||`, `!` and `?:` test a real for nonzero.
- Directory iteration in `Scrape` and `IncludeResolver` reports errors instead of
  throwing.

**Smaller:**

- `INT64_MIN / -1` wraps; integer division by zero is a warning; real division follows
  IEEE; a real outside the integer range saturates, and NaN reads as 0.
- `1e3` is a real. A block comment or string spanning lines no longer ends the line it
  started on, so a `#define` continues past it.
- Directive diagnostics carry the directive's location, an unclosed conditional points
  at its `#if`, an empty `#if` says so, and an expression that ends early reports where.
- `&&`, `||` and `?:` do not evaluate the side already decided, so `#if defined(X) &&
  X > 2` is quiet under strict mode.
- `char` is unsigned, as the build's `-funsigned-char` makes it; `long` is 32 bits.

**Afterwards, the front end moved out of `z64/`.** It had grown into a general C front end
with a few Zelda rules buried inside it, so it became `cparser/`, the make evaluator moved
out of `hackeroot/` into `makeparser/`, and `Diagnostic` and the width aliases moved to
`base/`, which every module shares. `z64/` keeps what is actually about the game, and the
Zelda rules that had been buried in the front end are now handed to it from there:
`getDecompDialect` (the N64's type widths, the decomp's typedefs, `TEX_LEN` and
`ARRAY_COUNT`) and `resolveTextureInclude` (the `.inc.c` to `.png` rule).

## 4. Workspace (done)

Everything that talks to the user's HackerOoT checkout. See
[docs/workspace.md](docs/workspace.md).

- `hackeroot::findCheckoutRoot` — any folder inside a checkout finds its root
- `hackeroot::findWslPath` — `\\wsl.localhost\Ubuntu\x` to distribution and Linux path,
  for the build runner
- Build settings persisted under `[build]` in `config.toml` and handed to the workspace as
  make overrides; `readBuildDefines` now also reports `VERSION`
- `hackeroot::readTables` — scenes, actors, objects and entrances as typed entries, read
  against the configuration; an entry with too few arguments keeps its ID and is warned
  about
- `hackeroot::listAssets` — scene and object folders from `assets/` and
  `extracted/<VERSION>/assets/`
- `hackeroot::Workspace` — all of the above for one checkout and one set of settings
- `toki::workspace::WorkspaceService` — reads on a worker thread, polls the watched files
  once a second, and reads again on a change; `WorkspacePanel` shows the tables

Windows delivers no change notifications for `\\wsl.localhost`: a `FileSystemWatcher` on
the share saw nothing while a file changed under it. Watching is therefore polling, at
about a millisecond per file. Against the real checkout a read takes about two seconds in
a Debug build, with no warnings; `CheckoutTest` loads it as a workspace.

Along the way, a panel the saved layout has never seen now opens by its default rather
than closed: the settings keep closed panel IDs beside the open ones.

Known limits: the scraped headers under `include/` are not watched (a quarter of a second
per poll); reopening the checkout picks up a change there. A panel without a saved dock
position, such as the new Workspace panel in an existing layout, opens floating until the
layout is reset. Quitting during a read waits for that read to finish.

## 5. N64 renderer, F3DEX2

Our own renderer, replacing the Fast3D spike. The spike's research found Fast3D usable
only through a translation pass and short on accuracy; RT64 accurate but too heavy to
embed. See [docs/renderer.md](docs/renderer.md). Writing it is not the risk; proving it
accurate is, so every stage that can run without a GPU does, under tests.

Hand-assembled display lists stand in for the memory image, which arrives in package 8.
Tests build their words with a small helper of their own, not with `gbi.h`.

- **RSP** (`n64/`, done) — F3DEX2 commands in, triangles out: the matrix stack, vertex
  transform and lighting, texture coordinate generation, culling, segments, `G_DL` and
  `G_ENDDL`, `G_MOVEWORD`, `G_MOVEMEM`. An unknown opcode is a diagnostic naming it and
  the address it was found at. Clipping is left to the GPU: triangles leave in clip
  space. What is approximated and what is not supported is listed in
  [docs/renderer.md](docs/renderer.md).
- **Texture memory** (`n64/`, done) — `LoadBlock`, `LoadTile`, `LoadTLUT`, tile
  descriptors, the swapped odd rows, wrap, mirror, clamp and masks. Load commands in,
  texels out. Filtering and level-of-detail selection belong with the GPU stage.
- **Combiner and blender** (`n64/`, done) — one and two cycle, as data a shader can read; a
  CPU evaluation of the same equations for tests. `runRdp` pairs every primitive with
  the RDP state and the texture memory it is drawn under, which is what the GPU stage
  uploads.
- **GPU** (`gfx/`, `toki_gfx`, done) — SDL3 GPU device, one ubershader compiled at build
  time for each backend, N64 three-point filtering, depth modes, fog. Texture memory
  reaches the shader raw, as a storage buffer, and is decoded there by the rules of
  `fetchTexel`. Blending that reads the framebuffer maps onto the GPU's fixed-function
  blend modes; blender cycles that do not read it run in the shader.

Package 5 is done. A hand-written display list with a lit, textured, fogged two-cycle
triangle renders offscreen on D3D12 and on Vulkan, and `toki_gpu_tests` compares the
shader with the CPU reference: the combiner, texel decoding and filtering (every format
family, palettes, wrap, mirror, clamp and the odd-row swap) and the shader-side blender
match it exactly, and translucency over the framebuffer comes within three steps of it.
docs/renderer.md lists what is not reproduced yet.

How the open questions were settled: shaders are compiled by `shadercross` from
vcpkg's `sdl3-shadercross`; the shader is tested in `toki_gpu_tests`, apart from
`toki_tests`; and the pixel reference for now is the CPU reference itself. A comparison
against RT64 or an emulator needs real scenes, and moves to packages 9 and 10.

## 6. Build runner (done)

Self-contained, and the highest day-to-day value in the plan. Also the second consumer of
the panel infrastructure, which is when its design gets tested.

- `hackeroot::makeBuildCommand` — `wsl.exe --exec make -jN SETTINGS... [run]`: a checkout
  on a WSL share runs in its distribution with `--cd` to its Linux path, any other path in
  the default distribution. `--exec` runs make without a shell, so no setting is expanded
  or split. The settings are the persisted `[build]` table, the same one the workspace
  reads.
- `toki::build::Process` — a child process whose output and errors share one pipe, in a
  Windows job object so stopping it ends everything it started; killing `wsl.exe` was
  checked to end the Linux side too. `WSL_UTF8` makes `wsl.exe`'s own messages UTF-8.
- `LineDecoder` — lines as they arrive in any chunking, `\r` redraws collapsed, and every
  byte that is not UTF-8 replaced by U+FFFD rather than dropped.
- `parseAnsiLine` — SGR colour (16, 256, 24-bit), bold, italic, underline; other escapes
  and control characters dropped.
- `BuildRunner` — one build at a time on a worker thread, output moved into a capped
  `BuildLog` once a frame, an outcome with exit code and time when it ends.
- `BuildPanel` and a Build menu: Build (Ctrl+B), Build and run (F5), Stop; the outcome
  also goes to the log.

Tests run real processes through `cmd.exe`, and `CheckoutTest`'s configuration runs
`make --version` through `wsl.exe` in the checkout's distribution.

Known limit: processes are only implemented on Windows; elsewhere starting a build reports
that.

Ports from `toki64/src/build_runner.rs`, `ui/{build_log_panel,ansi}.rs`.

## 7. Asset browser (done)

- Browse scenes, objects, textures from the workspace enumeration
- Display names from the scene table
- Select and open

"Open" needs a terminus before a renderer exists: run whatever parsing exists and report
to the log — file set, symbol counts, unknown macros, unhandled conditionals. Every later
parser package widens what this reports, which makes the UI the test harness for the rest
of the plan.

Keep it thin. No properties panel, no scene tree yet — those assume a data model that
package 10 can still move.

Done when opening any scene in the checkout logs its file set and diagnostic counts
without a crash, and `CheckoutTest` does the same for every scene.

Ports from `toki64/src/ui/asset_browser.rs`.

What was built: an Assets panel with Scenes and Objects tabs, each a sortable tree-table
over a details pane. Scenes are grouped by their folder and named from the scene table
(`SCENE_HYRULE_FIELD` reads "Hyrule Field"), with columns for folder, scene ID, room
count and source (vanilla or fast64); the pane shows the enum, path, title card and draw
config, the entrances that lead in, and the last open's counts. The filter matches names,
folders and enums. Room counts come from the folder listing the workspace already makes.
Double-clicking opens an asset: `AssetReader`
preprocesses and parses every C file of its folder on a worker thread, through
`hackeroot::preprocessAsset`, which holds the checkout's include conventions, and the
log gets each file's declarations and images, every diagnostic, and a summary. The
workspace is now shared, so a read keeps the workspace it started with alive when the
checkout is read again.

Against the real checkout all 103 scenes open: 500 files, 48,807 declarations, 3,951
images, no errors and no warnings, which the checkout test now holds. Getting there
found one front-end gap: prerendered rooms size their backgrounds with `sizeof(u64)`, so
the evaluator now answers `sizeof` of a type name from the dialect. The remaining ~9,900
notes are the includes the parser deliberately does not follow, such as `ultra64.h`.

Textures are not browsable yet: the workspace does not list them, and showing one needs
the PNG reader of package 8. Opening an object reports it the same way as a scene.

## 8. gbi word emission, texture encoding and the memory image

The rest of the parsing layer, plus the one component with no prototype equivalent. The
const-expr evaluator moved into package 2, which needed it for `#if` and array bounds.
The word format is settled by package 5: real N64 words, two `u32` with segment
addresses, which is what the renderer reads.

**gbi word emission** — which `gbi.h` encodes the words is decided here; see
[docs/gbi.md](docs/gbi.md). Either way the header encodes the words, not a hand-written
table. `gsDPLoadTextureBlock` expands to seven commands, which is what rules a
hand-written table out. Unknown macros must produce a diagnostic naming the macro; the set
must be user-extensible. The prototype's `gfx_decode.rs` tests are a source of known-good
word values. The same scrape of `gbi.h` that drives the dispatcher can likely drive a
disassembler, which would make the display list inspector mostly generated code; check
while building it.

`Expression` needs one addition here: `&sym[3]` as a symbol reference plus an offset, which
needs symbol addresses and so could not be built before the memory image.

*Done:* the checkout's own `gbi.h`, read by `hackeroot::readGbi` under the build's
configuration (which now starts with MIPS GCC's predefines, `_LANGUAGE_C` among them).
`z64::DisplayListEncoder` expands each element and evaluates its words, brace elision
included. `cparser` gained `#`, `##` and GNU's comma paste, multi-word type names,
aggregate `sizeof`, and symbol addresses with pointer arithmetic (`findSymbol`). Every
display list in the vanilla scenes and objects encodes without a diagnostic, and the common
commands match F3DEX2's known words. See [docs/gbi.md](docs/gbi.md).

**Texture encoding** — the reverse of decoding, and missing until now. Every vanilla
texture reaches the front end as a `.png` reference, while a segment buffer needs N64
bytes. So: a PNG reader (none is in the vcpkg manifest yet), and encoders for each format
the extracted assets use, ci4 and ci8 included, with the palette the image's name
records. The build does this with its own tools; match their output byte for byte, and
test against files the build produced.

*Done:* libspng reads the PNG; `n64::packTexels`, `packColorIndices` and `packPalette` pack;
`z64::parseTextureName` reads the build's suffix grammar (format, `.u32`, `tlut_<name>[_u32]`,
`split_lo`/`split_hi`); `z64::TextureEncoder` makes images, shared and unshared palettes,
JPEGs and binaries, padded as the build pads them, with shared palettes gathered across the
`assets/` and `extracted/` overlay. A checkout test compares 519 generated files across eight
asset folders with the build's output: all identical. Quantization is refused with a
diagnostic.

**Memory image** — per-segment buffers, a symbol table, laying out `Vtx[]`, `Gfx[]` and
texture data, and patching symbol references into segment addresses.

This is where the architecture is either right or wrong. Budget for it: there is nothing
in the prototype to copy.

Done when a fast64 room's C files and a vanilla room's C and PNG files both produce a
segment buffer and a display list entry offset, with byte-level tests.

*Done:* `hackeroot::readAssetImage` reads each file's system headers as a prelude under the
configuration (`readPrelude`, expanded as read), lays out their types (`cparser::parseTypes`),
encodes every object by its type (`z64::DataEncoder`, `Gfx` through `DisplayListEncoder`)
with addresses as relocations, and lays segments out and patches them
(`z64::layOutImage`); `z64::mapImage` hands the result to the RSP. A fixture fast64 room is
checked byte for byte. Against the checkout, every scene and room (500 files, vanilla and
the checkout's own) and every object is byte-identical to the `.data` of the object file
the build made, the address-named extracted symbols sit at the offsets their names record,
and the RSP runs a room's display list from the image. See
[docs/memory-image.md](docs/memory-image.md).

## 9. Viewport

The app's window still draws ImGui through OpenGL, while the renderer draws through SDL3's
GPU API. Showing a render target in a panel needs ImGui on the same device, which is its
SDL GPU backend (`imgui` feature `sdlgpu3-binding`): the window moves off OpenGL first.

- The renderer drawing a memory image
- Segment table set from the image
- Render to an offscreen framebuffer, presented as an ImGui image
- Orbit camera and projection

Done when a hand-built memory image renders in a dockable viewport panel.

*Done:* the window draws ImGui through SDL3's GPU API (`sdlgpu3-binding`) on the same
`gfx::Device` the renderer uses; OpenGL is gone. `toki::viewport::runFrame` writes a frame
display list of its own into segment 1, as the game builds one at run time: a `Vp` sized to
the panel, the `OrbitCamera`'s projection and view as `Mtx` (`n64::makeFixedMatrix`), then a
call to each of the image's display lists, and runs the RSP and RDP over the image and the
frame. Words are written by `n64::DisplayListWriter`, checked against the tests' own builder
and the RSP. The hand-built image is `makeReferenceGrid`, a checkerboard floor in segment 6,
shown until a scene is opened. `ViewportPanel` renders into an offscreen target sized to the
panel, only when the camera, size or image changed, and shows it as an ImGui image: left drag
orbits, right or middle drag (or Shift) pans, the wheel zooms. Tests check that the camera's
target lands at the screen's centre and the fixed-point matrices agree with the camera's.

## 10. Scene and room parsing

- `SCENE_CMD_*` header commands
- Room list, mesh headers, locating room display lists
- Actor and spawn lists, whose rotations go through `DEG_TO_BINANG`
- Environment and light settings

Done when picking a scene in the asset browser renders it, and editing that scene's
export in fast64 re-renders it without a restart.

Ports from `z64-parser/{scene,room,lib}.rs`.

*Done:* opening a scene in the asset browser reads it on `toki::scene::SceneLoader`'s
worker: `hackeroot::findSceneFiles` groups the folder into the scene's and each room's
files, and `hackeroot::AssetImageReader` reads them as one image each, the game's
`z_rcp.c` and `z_scene_table.c` first as segment 0, keeping the preludes between reads.
`readSceneHeader` and `readRoomHeader` read the headers out of the image's bytes as
`Scene_ExecuteCommands` does: rooms, player entries, light settings and light mode, room
shapes of all three types, actor lists. The viewport draws every room the way `Play_Draw`
does: `gSPSegment` to each room in turn, projection times view with an identity modelview,
lights and fog from the light setting, `sDefaultDisplayList` and `sSetupDL[SETUPDL_25]` from
the checkout's code, the opaque pass then the translucent one. Kokiri Forest, the Deku Tree
and a fast64 export render textured and lit. The loader polls the scene's files and reads
it again when one changes; a test edits a scene in a copy of the fixture checkout and sees
it read again, and a fast64 scene reads again in 0.4 s. See [docs/scenes.md](docs/scenes.md).

Left for later, each named where it stands in for the real thing: the spec instead of
file names for a segment's files, draw configs other than the default, alternate headers,
a choice of light setting, and actors and spawns drawn in the viewport.

---

## Remaining, after the vertical slice closes

**Objects** — object parsing, skeleton limb trees, animations, pose evaluation, an
animation transport panel. Ports from `z64-data/object.rs`,
`toki64/src/editor/animation_player.rs`.

**Display list inspector** — the RSP in `n64/` already decodes every word, so the
disassembler is a printer over its decoder rather than new work. What the renderer knows
on top (the RDP state behind a triangle, the combiner a material resolves to) is what
makes this panel worth more than the prototype's.

**Scene properties** — env lighting picker, fog, live updates.

*Done in part:* the Scene panel picks the layer (ages, times of day, cutscenes, read from the
alternate headers) and the light setting, shows and hides rooms, lists every room's actors
by name, and turns the camera to one. Actors, player entries and transition actors are
marked in the viewport. See [docs/scenes.md](docs/scenes.md#showing-it). Editing fog and
light settings is still to do.

**Tools** — Configure, Create Actor, Register Scene. All depend on the workspace tables.
`scene_registry.rs` is the largest single file in the prototype (~1200 lines) and ports
close to as-is.

## Deliberately not in the port

New features the prototype never had. They are the reason the tool exists, but they come
after parity — with one candidate for jumping the queue.

**Actor render database** — actors drawn as their real objects instead of cubes. The
motivating feature, and a candidate to pull forward as soon as objects render. It needs
more than it first appears to. The object an actor uses is in its `ActorProfile`
initializer, which the parser reads today. The skeleton and animation are not: they are
arguments to calls inside function bodies (`SkelAnime_InitFlex(play, …, &gXxxSkel,
&gXxxAnim, …)` in `Init`), and the parser steps over functions entirely. That needs a new
capability, matching call patterns inside function bodies, which is heuristic by nature
and needs its own design and estimate before it is scheduled.

**Message editor** — dialogue text with a live textbox preview.

**Audio** — soundfonts and sequences. Backlog.
