# Work packages

Porting the Rust prototype (`../Toki64`) to this codebase, in order.

The ordering puts the UI first on purpose. The shell, the workspace layer and the asset
browser are the same work no matter what ends up rendering, so none of it is at risk from
the Fast3D spike. Doing them first also means every parser package that follows can be
exercised from the running app instead of from unit tests alone. The one exception is the
spike itself, which sits right after the workspace: it depends on nothing before it, and
its answer decides the shape of package 8.

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

Wider than first planned, because the real checkout forced it. See the parsing strategy in
CLAUDE.md for why; the short version is that fast64 emits `#if` and function-like macros
inside asset `.c`, extracted assets include texture data from files the build generates
from `.png`, and array bounds come from `#define`s in a sibling header. None of that can
be read without a preprocessor.

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
`decompDialect` (the N64's type widths, the decomp's typedefs, `TEX_LEN` and
`ARRAY_COUNT`) and `resolveTextureInclude` (the `.inc.c` to `.png` rule).

## 4. Workspace

Everything that talks to the user's HackerOoT checkout.

- Root discovery and validation
- Windows/WSL path translation
- Build settings persisted, and handed to `readBuildDefines` as make overrides. The
  scrape and `Configuration` are owned for the session, in that order, since the layer
  points at the scrape
- Ordinal table readers: scene, entrance, actor, object, read against the configuration
- Asset enumeration under `assets/`
- **Watching the checkout.** The edit loop is why this tool exists: export from fast64,
  see the change. The architecture already allows a reload (a new unit per read, the old
  one valid until dropped), but nothing triggers one. Watch the checkout; a changed asset
  file invalidates the units that read it, and a changed Makefile or `include/config/`
  header rebuilds the configuration and rereads the tables

Done when the app can point at a checkout, list its scenes, objects and actor IDs, and
lists them again on its own after `config_debug.h` changes.

Ports from `toki64/src/hackeroot_tables.rs`, path handling in `build_runner.rs`.

## 5. Fast3D extraction spike

The one genuinely risky package. **Timebox it and name the box up front.**

It is not mainly an extraction question. Getting Fast3D to compile is necessary; what
decides package 8 is whether Fast3D can consume the words our memory image will produce.
Answer these first, before any plumbing:

- **Word format.** libultraship moved Fast3D to PC display lists. Check whether its `Gfx`
  is still two `u32` words carrying segment addresses, or two pointer-sized words carrying
  host pointers. If the latter, the choice is a 32-bit word path in the vendored copy or a
  translation pass, and the verdict must say which.
- **Which `gbi.h` encodes the words.** CLAUDE.md says the `gs*` words come from vendored
  Fast3D's `gbi.h`. If that header is the PC variant, it encodes PC words. The faithful
  alternative is HackerOoT's own `include/ultra64/gbi.h` compiled with the `GBI_DEFINES`
  the configuration layer already reads, which conflicts with "never parse decomp headers
  for gbi constants". Decide, and update CLAUDE.md with the decision.
- **F3DEX3.** HackerOoT supports `ENABLE_F3DEX3`, and fast64 can export for it. Find out
  whether Fast3D handles F3DEX3 commands. If not, "F3DEX3 checkouts are unsupported" goes
  into the plan as a decision, not left to surface as a bug.
- **Textures.** Does Fast3D decode N64 texture formats itself? If it does, decoding is off
  the rendering path and only needed later for a texture browser. It does not remove
  encoding; see package 8.

Then the plumbing:

- Find the minimal file set behind `gfx_pc` and the OpenGL backend
- Sever the libultraship resource manager, window, input and ImGui hooks
- Get it compiling under `third_party/Fast3D/`
- Draw one hand-written display list, in the word format package 8 will emit, into an
  offscreen framebuffer

Done when a demo renders Fast3D output, and there is a written verdict: usable as-is,
needs surgery, or abandon, with the four questions above answered.

If it is abandoned, the fallback is porting the prototype's renderer logic to OpenGL —
`z64-render` worked, so this is a known quantity rather than open-ended. Note that the
memory-image decision would also reopen in that case.

## 6. Build runner

Self-contained, and the highest day-to-day value in the plan. Also the second consumer of
the panel infrastructure, which is when its design gets tested.

- `wsl.exe -- make` and `make run`, streamed
- Make's command line built from the same persisted build settings the configuration
  layer reads
- Lossy UTF-8 decoding so non-UTF-8 output is not dropped
- Build log panel with ANSI colour

Done when a build can be started from the app and its output read as it arrives.

Ports from `toki64/src/build_runner.rs`, `ui/{build_log_panel,ansi}.rs`.

## 7. Asset browser

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

## 8. gbi word emission, texture encoding and the memory image

The rest of the parsing layer, plus the one component with no prototype equivalent. The
const-expr evaluator moved into package 2, which needed it for `#if` and array bounds.
Which `gbi.h` and which word format are settled by package 5.

**gbi word emission** — a name-to-value map generated from the chosen `gbi.h`, and a
generated dispatcher that calls the real dynamic `g*(pkt, ...)` macro for each `gs*` name
with the evaluated arguments, so the SDK header encodes the words rather than a
hand-written table. `gsDPLoadTextureBlock` expands to seven commands, which is what rules a
hand-written table out. Unknown macros must produce a diagnostic naming the macro; the set
must be user-extensible. The prototype's `gfx_decode.rs` tests are a source of known-good
word values. The same scrape of `gbi.h` that drives the dispatcher can likely drive a
disassembler, which would make the display list inspector mostly generated code; check
while building it.

`Expression` needs one addition here: `&sym[3]` as a symbol reference plus an offset, which
needs symbol addresses and so could not be built before the memory image.

**Texture encoding** — the reverse of decoding, and missing until now. Every vanilla
texture reaches the front end as a `.png` reference, while a segment buffer needs N64
bytes. So: a PNG reader (none is in the vcpkg manifest yet), and encoders for each format
the extracted assets use, ci4 and ci8 included, with the palette the image's name
records. The build does this with its own tools; match their output byte for byte, and
test against files the build produced.

**Memory image** — per-segment buffers, a symbol table, laying out `Vtx[]`, `Gfx[]` and
texture data, and patching symbol references into segment addresses.

This is where the architecture is either right or wrong. Budget for it: there is nothing
in the prototype to copy.

Done when a fast64 room's C files and a vanilla room's C and PNG files both produce a
segment buffer and a display list entry offset, with byte-level tests.

## 9. gfx binding and viewport

- Fast3D initialised on the OpenGL backend
- Segment table upload
- Render to an offscreen framebuffer, presented as an ImGui image
- Orbit camera and projection

Done when a hand-built memory image renders in a dockable viewport panel.

## 10. Scene and room parsing

- `SCENE_CMD_*` header commands
- Room list, mesh headers, locating room display lists
- Actor and spawn lists, whose rotations go through `DEG_TO_BINANG`
- Environment and light settings

Done when picking a scene in the asset browser renders it, and editing that scene's
export in fast64 re-renders it without a restart.

Ports from `z64-parser/{scene,room,lib}.rs`.

---

## Remaining, after the vertical slice closes

**Objects** — object parsing, skeleton limb trees, animations, pose evaluation, an
animation transport panel. Ports from `z64-data/object.rs`,
`toki64/src/editor/animation_player.rs`.

**Display list inspector** — this one grew. The prototype decoded into a typed
`GfxCommand` enum, so the panel was nearly free. We emit raw words, so this now needs a
real disassembler: words back to readable commands. New work, not a port, unless package 8
finds it can be generated from the same `gbi.h` scrape as the dispatcher. Not a reason to
reverse the architecture, but do not budget it as "port the panel".

**Scene properties** — env lighting picker, fog, live updates.

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
