# Work packages

Porting the Rust prototype (`../Toki64`) to this codebase, in order.

The ordering puts the UI first on purpose. The shell, the workspace layer and the asset
browser are the same work no matter what ends up rendering, so none of it is at risk from
the Fast3D spike. Doing them first also means every parser package that follows can be
exercised from the running app instead of from unit tests alone.

Each package should be independently committable. Parsers and table readers get tests.

---

## 1. App shell

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

## 2. z64 scanner and table reader

The first slice of the parsing layer — deliberately the least uncertain part.

- Token scan of decomp C
- `#include` following, scoped to the asset directory only
- Preprocessor directives recognized and reported, not evaluated
- X-macro table reader producing ordinal rows

The ordinal behaviour is the point. `SceneID` and `ActorID` come from position in the
table, so a conditionally-disabled entry read as live shifts every later ID. An unhandled
conditional in a table file is a loud warning, never a silent skip.

Done when the readers have tests over fixture tables, including one with a conditional.

Replaces `c-parser/*`. Expect far less code than the prototype's 3.2k lines.

## 3. Workspace

Everything that talks to the user's HackerOoT checkout.

- Root discovery and validation
- Windows/WSL path translation
- Ordinal table readers: scene, entrance, actor, object
- Asset enumeration under `assets/`

Done when the app can point at a checkout and list its scenes, objects and actor IDs.

Ports from `toki64/src/hackeroot_tables.rs`, path handling in `build_runner.rs`.

## 4. Build runner

Self-contained, and the highest day-to-day value in the plan. Also the second consumer of
the panel infrastructure, which is when its design gets tested.

- `wsl.exe -- make` and `make run`, streamed
- Lossy UTF-8 decoding so non-UTF-8 output is not dropped
- Build log panel with ANSI colour

Done when a build can be started from the app and its output read as it arrives.

Ports from `toki64/src/build_runner.rs`, `ui/{build_log_panel,ansi}.rs`.

## 5. Asset browser

- Browse scenes, objects, textures from the workspace enumeration
- Display names from the scene table
- Select and open

"Open" needs a terminus before a renderer exists: run whatever parsing exists and report
to the log — file set, symbol counts, unknown macros, unhandled conditionals. Every later
parser package widens what this reports, which makes the UI the test harness for the rest
of the plan.

Keep it thin. No properties panel, no scene tree yet — those assume a data model that
package 8 can still move.

Ports from `toki64/src/ui/asset_browser.rs`.

## 6. Fast3D extraction spike

The one genuinely risky package. **Timebox it and name the box up front.**

- Find the minimal file set behind `gfx_pc` and the OpenGL backend
- Sever the libultraship resource manager, window, input and ImGui hooks
- Get it compiling under `third_party/Fast3D/`
- Draw one hand-written display list into an offscreen framebuffer

Done when a demo renders Fast3D output, and there is a written verdict: usable as-is,
needs surgery, or abandon.

If it is abandoned, the fallback is porting the prototype's renderer logic to OpenGL —
`z64-render` worked, so this is a known quantity rather than open-ended. Note that the
memory-image decision would also reopen in that case.

Also answer one question while in there: **does Fast3D decode N64 texture formats itself?**
If it does, texture decoding is off the rendering path entirely and is only needed later
for a texture browser.

## 7. Const-expr, gbi word emission, memory image

The rest of the parsing layer, plus the one component with no prototype equivalent.

**Const-expr evaluator** — integer literals, `|`, `<<`, negation. Small.

**gbi word emission** — a name-to-value map generated from the vendored `gbi.h`, and a
table mapping each `gs*` macro plus its arguments to the 32-bit words it encodes. Unknown
macros must produce a diagnostic naming the macro; the table must be user-extensible.
The prototype's `gfx_decode.rs` tests are a source of known-good word values.

**Memory image** — per-segment buffers, a symbol table, laying out `Vtx[]`, `Gfx[]` and
texture data, and patching symbol references into segment addresses.

This is where the architecture is either right or wrong. Budget for it: there is nothing
in the prototype to copy.

Done when a fast64 room's C files produce a segment buffer and a display list entry
offset, with byte-level tests.

## 8. gfx binding and viewport

- Fast3D initialised on the OpenGL backend
- Segment table upload
- Render to an offscreen framebuffer, presented as an ImGui image
- Orbit camera and projection

Done when a hand-built memory image renders in a dockable viewport panel.

## 9. Scene and room parsing

- `SCENE_CMD_*` header commands
- Room list, mesh headers, locating room display lists
- Environment and light settings

Done when picking a scene in the asset browser renders it.

Ports from `z64-parser/{scene,room,lib}.rs`.

---

## Remaining, after the vertical slice closes

**Objects** — object parsing, skeleton limb trees, animations, pose evaluation, an
animation transport panel. Ports from `z64-data/object.rs`,
`toki64/src/editor/animation_player.rs`.

**Display list inspector** — this one grew. The prototype decoded into a typed
`GfxCommand` enum, so the panel was nearly free. We emit raw words, so this now needs a
real disassembler: words back to readable commands. New work, not a port. Not a reason to
reverse the architecture, but do not budget it as "port the panel".

**Scene properties** — env lighting picker, fog, live updates.

**Tools** — Configure, Create Actor, Register Scene. All depend on the workspace tables.
`scene_registry.rs` is the largest single file in the prototype (~1200 lines) and ports
close to as-is.

## Deliberately not in the port

New features the prototype never had. They are the reason the tool exists, but they come
after parity — with one candidate for jumping the queue.

**Actor render database** — actors drawn as their real objects instead of cubes. Needs
only packages 2, 3 and Objects. Since it is the motivating feature, it is a reasonable
candidate to pull forward as soon as objects render, rather than waiting for full parity.

**Message editor** — dialogue text with a live textbox preview.

**Audio** — soundfonts and sequences. Backlog.
