# The actor database

Design for a database that says how each actor is drawn, edited in the application, with
the C reading of [actor-rendering.md](actor-rendering.md) as the source of first guesses
rather than of the answer. Steps 0 to 2 of its plan are built: the format, read and
written, and scenes drawn from it; the rest is the design still to build.

## Why

Reading literals out of an overlay's `Init` and `Draw` draws 72% of the checkout's actors,
and that is close to its ceiling. Ice Cavern drew 18 of its 24 actors, and several of those
were wrong:

- **Params.** What an actor draws often depends on its params, through a table
  (`sDLists[params & 0xF]`), a `switch`, or a branch. The C names every candidate; which one
  a placed actor uses is decided at runtime.
- **Size.** Scales set from params, from tables, or in `Update`, are not literals. Neither is
  a scale that changes with the actor's state.
- **Defaults.** The animation an actor stands in is often picked by a setup function from a
  table (`sAnimationInfo`), so the rules draw it at rest.

These cannot be read without running the code, which is ruled out. What they can be is
written down, once per actor, by someone who knows what the actor looks like. The database
is where that is written. The C reading stays, as the tool that fills an entry in so nobody
starts from nothing.

## What changes

- **The database is the source of truth.** An actor with an entry is drawn as its entry
  says. Its C is not read for drawing.
- **Reading the C only guesses.** A guess is labelled as one wherever it shows, and it never
  overwrites an entry a person wrote or checked.
- **Actor code is still never run.**

This replaces two decisions of [actor-rendering.md](actor-rendering.md): "only literals
count" and "no overrides for now". The invariants in CLAUDE.md change with it, in the step
that builds the database.

## An entry

An entry is keyed by the actor's enum name, `ACTOR_EN_ANI`, not its ID: an ID moves when an
`#if` in the actor table changes, the name does not.

```toml
# <checkout>/toki/actors.toml
format = 1

[actors.ACTOR_OBJ_TSUBO]
status = "checked"
object = "gameplay_dangeon_keep"
scale = 0.15

[actors.ACTOR_OBJ_TSUBO.fields]
type      = { kind = "enum", shift = 8, width = 1, values = { dungeon = 0, field = 1 } }
drop      = { kind = "integer", shift = 0, width = 5 }
breakable = { kind = "flag", shift = 6 }

[[actors.ACTOR_OBJ_TSUBO.variants]]
match = { type = "dungeon" }
display_lists = [{ name = "gPotDungeonDL" }]

[[actors.ACTOR_OBJ_TSUBO.variants]]
match = { type = "field" }
object = "gameplay_field_keep"
display_lists = [{ name = "gPotFieldDL" }]

[actors.ACTOR_EN_ANI]
status = "guessed"
guessed_from = { file = "src/overlays/actors/ovl_En_Ani/z_en_ani.c", modified = 2026-10-06T21:14:03Z }
object = "object_ani"
scale = 0.01

[[actors.ACTOR_EN_ANI.variants]]
skeleton = "gRoofManSkel"
animation = "gRoofManIdleAnim"
frame = 0
segments = { 8 = "gRoofManEyeOpenTex" }

[actors.ACTOR_BG_SPOT00_HANEBASI]
status = "checked"
object = "object_spot00_objects"
rotates = { x = false, y = true, z = false }

[actors.ACTOR_BG_SPOT00_HANEBASI.fields]
part = { kind = "enum", source = "rotation_z", shift = 0, width = 2, values = { bridge = 0, chain = 1 } }

[[actors.ACTOR_BG_SPOT00_HANEBASI.variants]]
match = { part = "chain" }
display_lists = [
    { name = "gDrawbridgeChainDL", translation = [-200, 0, 0] },
    { name = "gDrawbridgeChainDL", translation = [200, 0, 0] },
]

[actors.ACTOR_EN_LIGHT]
status = "checked"
draw = "marker"
```

The symbol names in these examples show the format; they are not checked against the
checkout's objects.

- **`status`** is `guessed` or `checked`. A guess becomes checked when a person edits or
  confirms it. Every entry of the shipped database is checked.
- **`guessed_from`** is the overlay a guess was read from, and its modification time then.
  An overlay that has changed since makes the guess stale, which the panels show.
- **Actor-wide values** (`object`, `scale`, `height_offset`) apply to every variant that
  does not set its own.
- **`fields`** name ranges of bits, and say what kind of value they hold (see
  [Fields](#fields)). The Scene panel shows a placed actor's params decoded by them.
- **`variants`** are tried in order. The first whose `match` holds for a placed actor is
  drawn. A variant without `match` matches everything, so it is the default, and belongs
  last. A `match` lists fields and the value each must have, by number or by value name, or
  a list of values any of which will do (`type = ["small", 6]`). `age` and `scene` are fields
  every actor has without declaring them: `scene` is the scene's enum name from the scene
  table (`scene = "SCENE_DEKU_TREE"`), for actors that pick what they draw by the scene they
  are in, as `Door_Shutter` does.
- **What a variant draws** is a `skeleton` with an optional `animation` and the `frame` of
  it to stand on (0 when left out), or a list of `display_lists`, or `draw = "marker"` for an
  actor that has no model by design. Every symbol is by name, in the variant's object, or
  in its `animation_object` for an animation an actor takes from another object.
- **A display list** may name its own `object`, has a `pass` of `opaque` (the default) or
  `translucent`, and may be
  placed relative to the actor by a `translation`, a `rotation` in degrees and a `scale` of
  its own, applied after the actor's matrix: the parts of gates, chains and bosses, which
  their code places at offsets it computes. The same list may be drawn more than once.
- **`rotates`** says which of the placed rotation's axes the actor is drawn turned by, all
  three when left out. An actor that reads parameters from its rotation usually draws
  unturned on those axes.
- **`segments`** point segments 8 to 12 at a symbol of the object, eyes and mouths. A display
  list may carry its own `segments`, as effects actors point segment 8 at a different texture
  for each list.

### Fields

A field is a range of bits of the placed actor's params, or of one axis of its placed
rotation, which some actors read extra parameters from (`source` is `params`, the default,
or `rotation_x`, `rotation_y`, `rotation_z`). Its `kind` says what the bits mean, and with it
how the editor shows them:

| Kind | Bits | Shown as | Matched as |
|---|---|---|---|
| `flag` | one, at `shift` | a checkbox | `breakable = true` |
| `enum` | `width` at `shift` | a dropdown of its value names | `type = "field"`, or a number |
| `integer` | `width` at `shift`, `signed` or not | a number field, in hex when `hex = true` | a number |

An enum's `values` are its constants: each a name and the number it stands for. They are
edited in the Actors panel like any other part of the entry: added, renamed, removed. A
placed actor whose value has no name is shown by its number, so nothing is hidden. A value
must fit its field's width, names are unique within a field, and two fields of one source
may not share a bit; each is a diagnostic naming the actor and the field.

`age` is a field every actor has without declaring it: an enum of `child` and `adult`, from
the scene layer (see [Player](#player)).

More kinds can come later without changing the rest: a `switch flag` or `collectible flag`
is an integer the editor could link to a view of which flags a scene uses.

### Everything else an entry says

What an actor is and does, beyond its model, so the panels can say it and the viewport can
show it. All of it is optional.

```toml
[actors.ACTOR_EN_KAREBABA]
name = "Withered Deku Baba"
description = "A Deku Baba that only attacks when Link comes close, and drops a Deku Stick."
notes = "The stem is drawn in code from three lists; the variants place them."
shadow = { kind = "circle", size = 22 }
spawn = { age = "child" }

[actors.ACTOR_EN_KAREBABA.fields]
switch = { kind = "integer", shift = 8, width = 6, role = "switch_flag", meaning = "Set when it is cut down" }

[[actors.ACTOR_EN_KAREBABA.variants]]
skeleton = "gDekuBabaSkel"
animation = "gDekuBabaFastChompAnim"
default_params = 0x0000

[actors.ACTOR_OBJ_MURE2]
name = "Ring of bushes or rocks"
draw = "children"

[[actors.ACTOR_OBJ_MURE2.variants]]
match = { type = "bushes" }
children = [
    { actor = "ACTOR_EN_KUSA", params = 0x0000, offset = [0, 0, 80] },
    { actor = "ACTOR_EN_KUSA", params = 0x0000, offset = [80, 0, 0] },
]

[actors.ACTOR_OBJ_SYOKUDAI]
name = "Torch"
collision = "gTorchCol"
lights = [{ color = [255, 160, 64], radius = 250, offset = [0, 60, 0] }]

[[actors.ACTOR_OBJ_SYOKUDAI.variants]]
display_lists = [
    { name = "gTorchDL" },
    { name = "gFlameDL", pass = "translucent", billboard = true, colors = { prim = [255, 255, 170, 255], env = [255, 64, 0, 255] } },
]

[actors.ACTOR_EN_GO2]
name = "Goron"
text_ids = [0x3000, 0x3001]
```

- **`name`, `description`, `notes`**: what a person calls the actor, a line on what it is, and
  anything that fits nowhere else: "cutscene only", "draws its eyes in code".
- **`colors`**: the primitive and environment colour a variant or one display list is drawn
  with, as `Draw` sets them: rupees, chests, tunics and fairies differ only in these.
- **`billboard`**: a variant or display list drawn facing the camera, as flames and sprites are.
- **`shadow`**: the shadow `ActorShape_Init` gives the actor, `none`, `circle`, `white_circle`,
  `horse` or `feet`, its size, and its `alpha` (`shape.shadowAlpha`, 255 when left out), drawn as `ActorShadow_Draw` draws it: after
  `sSetupDL[SETUPDL_44]`, `gCircleShadowDL` or `gHorseShadowDL` in black or white, where the
  actor stands, scaled by the size times the actor's scale. Feet are drawn as a circle, since
  where the feet are needs the pose of the game.
- **`children`**: what an actor spawns, each child as an actor, params and offset from its
  parent, turned with it. An actor that draws nothing itself but places others, as
  `Obj_Mure2` does, is `draw = "children"`; one that draws and spawns both has its children
  as well as its model. Each child is drawn by its own entry. Children of children are not
  followed.
- **`spawn`**: what the actor needs to exist: an `age`, a `time` of `day` or `night`, or a
  flag field that must be `set` or `unset` (`flag = { field = "switch", is = "unset" }`).
  The Scene panel greys out an actor whose age or time does not hold in the layer shown, and
  says which flag it needs. A variant may have its own.
- **A field's `role` and `meaning`**: what an integer field holds, `switch_flag`,
  `chest_flag`, `collectible_flag`, `clear_flag`, `path`, `text_id`, `item` or `actor`, and a
  line on what it is for. Roles let later panels link a field to a scene's flags, paths and
  messages.
- **`default_params`**: what a variant's params are when the actor is placed new.
- **`collision`**: the collision mesh a background actor registers with `DynaPoly`, by name,
  in its object, for an overlay of collision in the viewport.
- **`lights`**: the point lights an actor gives off, colour, radius and offset, for when the
  viewport shows actors' lights.
- **`text_ids`**: the messages an actor shows, where they do not come from a field, for when
  the message editor can be opened from an actor.

What the C states as plain file-scope data is read from it every time, not stored: the
actor's category from its profile, its collider's size from its `ColliderCylinderInit`,
and its draw distance from its init chain. An entry may still set any of them, to correct
a reading.

### Steps

What the keys above cannot say, a variant says as `steps`: how its `Draw` draws, one call
at a time, in order. A variant with steps draws by them alone, and the keys that say what
it draws (`skeleton`, `display_lists`, `segments`, `colors`, `billboard`) are an error beside
them. Fixed keys are turned into steps when read for drawing, so there is one way an actor is
drawn.

```toml
[[actors.ACTOR_EN_BOX.variants]]
match = { type = ["small", "small_room_clear"] }
steps = [
    { colors = { env = [0, 0, 0, 255] } },
    { transform = { rotation = [0, 180, 0], scale = 0.005 } },
    { skeleton = "gTreasureChestSkel", animation = "gTreasureChestAnim_00024C", limbs = {
        1 = [{ display_list = "gTreasureChestBaseDL" }],
        3 = [{ display_list = "gTreasureChestLidDL" }] } },
]
```

Each step is one of:

- **`setup`**: `Gfx_SetupDL` with an index of `sSetupDL`, for the display lists after it.
  Steps start at `SETUPDL_25`, as `Actor_Draw` leaves them.
- **`colors`**: the primitive and environment colour of what follows.
- **`hilite = true`**: `func_8002EBCC` or `func_8002ED80`, which `Draw` calls before what
  shines: what follows is drawn with a `LookAt` from the camera toward the actor, which
  `G_TEXTURE_GEN` maps its normals by. Without one, a draw has the frame's, from the camera
  toward what it looks at, standing in for whatever the game drew last. The hilite tile
  `gDPSetHilite1Tile` sets is not set.
- **`segment`** and **`symbol`**: points a segment from 8 to 12 at a symbol, of the step's
  `object` or the variant's, or at `gEmptyDL` of the game code, as `Draw` points a segment at
  nothing. Segments stay pointed for every step after, as they do in the game; ones no step
  points are at `gEmptyDL`.
- **`transform`**: a `translation`, `rotation` in degrees and `scale` applied to what follows,
  after those before it; `reset = true` starts again from the actor's matrix, and
  `billboard = true` turns it to face the camera.
- **`display_list`**: draws one, of its `object` or the variant's, in its `pass`.
- **`skeleton`**: draws one where the transforms put it, with its own `object`, `animation`,
  `animation_object`, `frame` and `pass`, and under `limbs` the steps each limb's
  `PostLimbDraw` draws with that limb's matrix, by the index `PostLimbDraw` is called with,
  1 for the root limb, so the numbers read as they do in the actor's C. A limb's steps start
  from the limb's matrix and draw display lists only. A variant draws at most one skeleton.
- **`shadow`**: the variant's own shadow, in place of the entry's.

### Player

Player has no overlay, so its entry is written by hand and shipped. Which model Link has
depends on his age, not on params, so `age` is a field every actor has without declaring it:
`child` in the scene's child layers, `adult` in its adult ones, unknown in cutscene layers,
where only a variant without an `age` match applies.

```toml
[[actors.ACTOR_PLAYER.variants]]
match = { age = "child" }
object = "object_link_child"
skeleton = "gLinkChildSkel"

[[actors.ACTOR_PLAYER.variants]]
match = { age = "adult" }
object = "object_link_boy"
skeleton = "gLinkAdultSkel"
```

Link stands at rest until his animations, which live in `link_animetion` rather than in
asset C, can be read. He is drawn at the entrance the camera is looking at, not at every
player entry of the scene at once.

## Two files

- **The shipped database**, `assets/actors.toml` in Toki's own repository, copied beside
  the executable with the fonts: curated, every entry checked, the vanilla actors as HackerOoT ships them.
  It is the same for everyone. It records the HackerOoT commit it was checked against
  (`checked_against = "<commit>"`), for reference.
- **The checkout's database**, `<checkout>/toki/actors.toml`: the mod's own actors, and any
  vanilla actor the mod changed. It travels with the mod, is committed with it, and is shared
  by everyone who works on it.

An entry in the checkout's file replaces the shipped entry for that actor whole, rather than
field by field: what is drawn is then always one entry, readable in one place. The editor
offers "Copy the shipped entry" to start a change from.

A shipped entry can name a symbol a checkout no longer has, when the mod changed a vanilla
actor. That entry is then not drawn, the actor is guessed instead, and the Scene panel says
why.

The checkout's file is watched like the asset files are: editing it by hand outside Toki
redraws the open scene.

## Guesses

The rules of [actor-rendering.md](actor-rendering.md) become a guesser: `makeGuessedEntry`
turns what they read into an entry with `status = "guessed"`.

- **On the fly.** An actor with no entry in either file is guessed when a scene places it,
  and drawn by the guess. Guesses are cached on disk, beside the application's config and
  per checkout, keyed by the overlay's path and modification time, so an overlay is read
  once until it changes.
- **Into the database.** "Keep this guess" writes a guess into the checkout's file, where it
  stays `guessed` until a person checks it.
- **Better guesses.** A table indexed by params is today a gap. As a guess, it becomes a
  field and one variant per entry: `sDLists[PARAMS_GET_U(this->actor.params, 8, 1)]` is a
  field of shift 8 and width 1, and a variant for each of its values drawing that entry of
  the table. The same goes for skeletons, scales and segments chosen from tables.

Re-guessing an entry that is checked never replaces it. It shows what the guess would change,
for the person to take or leave.

## Checking for new actors

Whenever the checkout has been read, at startup and whenever the actor table changes, the
actor table is checked against both files. A new actor is one the table lists, that is not
an unset slot, and that has no entry in either.

When there are new actors, Toki asks: "N new actors were found. Add them to the actor
database?", listing them. Yes guesses them all once, on a worker, and writes the guesses into
the checkout's file. No is remembered, in `config.toml` per checkout, until the set of new
actors changes, so the same ones are not asked about at every start. On a vanilla checkout
with the shipped database there are none, or only the ones the shipped file does not cover
yet.

The same check finds shipped entries that do not fit the checkout: an object the checkout
does not have, or a symbol its object does not declare, as when a mod changed a vanilla
actor. They are listed in one warning, naming each actor and what is missing. Those actors
are guessed instead, and "Copy the shipped entry" starts the checkout's own entry from the
shipped one, to fix.

## Editing

- **The Scene panel** shows each placed actor's params decoded by its fields, and its entry's
  status: checked, guessed, stale, marker, or none. "Edit actor" opens its entry with the
  variant that placement matches selected. "Add a variant for these params" adds a variant
  whose `match` is those params' fields, ahead of the default.
- **The Actors panel** lists every actor of the actor table with its status, filtered by name
  or status, and edits one entry: its fields, each with its kind, bits and source, and an
  enum's values; the axes it rotates by; its variants in order, and for each variant its
  match, edited by the fields' own controls (a checkbox, a dropdown, a number), what it
  draws, its object, scale, height offset, frame and segments, and each display list's pass
  and placement. Objects are picked from the checkout's objects, and skeletons, animations and
  display lists from the picked object's catalog, which already sorts its symbols by
  declared type ([objects.md](objects.md#the-catalog)).
- **Edits draw at once** in the open scene, and are saved to the checkout's file. The file is
  written by Toki with a stable order of actors and keys, alphabetical, without indentation,
  and with params in hexadecimal, so it diffs well. It is written beside itself first and
  then over itself, so a failed write leaves it as it was. Comments in it are not kept.

## Cost

Ice Cavern took 32 s to read its actors in a debug build over `\\wsl.localhost`. Measured
in a release build over `\\wsl.localhost`, with the hidden test "how long a scene's actors
take to read":

| Read | Actors | Overlays | Objects |
|---|---|---|---|
| First | 14.2–16.9 s | 0.9 s | 13.3–16.0 s, 14 objects |
| Again, same reader | 0.94 s | 0.8 s | 0.14 s |

The scene itself reads in 4.9–5.1 s. Of the objects, `gameplay_keep` with the game code is
6.6 s, then `object_haka_objects` 1.3 s, `object_xc` 0.9 s, `object_wf` 0.8 s, and the other
ten 0.05 to 0.7 s each: every object is a file open and a PNG decode per texture, and a file
open over the share costs milliseconds. Read again, each object takes about 10 ms.

So the overlays are not the cost, and the database saving their preprocessing saves under a
second. The objects are, and they are paid once per loader per session: the scene loader,
the actor loader and the Object panel each read `gameplay_keep` for themselves. In the order
they would help:

- **One image cache for the session**, shared by every loader, so `gameplay_keep` and every
  object is read once however it is first opened (`hackeroot::ImageCache`, done).
- **Images kept on disk**, keyed by their files' modification times, so a second session
  opens a scene's actors in about the second read's time. The first ever read stays slow.
- **Objects read in parallel**: the share's cost is latency, not bandwidth, so reading the
  fourteen objects at once should bring the first read down to about the slowest few. It
  needs the image reader's caches made safe to share between threads.

The first is done: a second reader of the workspace reads Ice Cavern's actors in 0.94 s,
where it read all fourteen objects again before. Step 9 picks from the other two.

## The curated run

The shipped database is written in one pass over every vanilla actor, by reading each
overlay and its objects by hand rather than by the guesser, because a person, or Claude,
reading the C can follow a `switch` on params where the rules cannot. Every key above is
filled in that pass, so no actor is read twice; that is why the format is settled first.

- **When:** after step 2, so the format and its validation exist and entries can be drawn.
- **In batches by category**: enemies, NPCs, props, background actors, items, effects and
  cutscene actors. The file itself is the progress, so a batch that ends a session is picked
  up where it stopped.
- **Checked by code**: after each batch, a checkout test reads the shipped file and fails on
  any entry whose objects or symbols the vanilla checkout does not have, or whose fields
  overlap.
- **Checked by eye where unsure**: an entry whose reading is uncertain is marked
  (`confidence = "low"`, with a note saying what), and those are drawn to images and looked
  at. Drawing all 430 would cost more than it finds. The hidden GPU test "every variant of
  the shipped actor database is drawn to an image" (`toki_gpu_tests "[.render]"`, against a
  checkout) draws each variant of every shipped entry, framed on its bounds, to
  `actor-renders/<ACTOR>_<variant>.png` beside the test binary, and fails on any diagnostic.
  It draws each variant by itself, not by params made up from its match, so a default
  variant is drawn too.
- **A trial of ten first**: `Obj_Tsubo`, `En_Kusa`, `Obj_Mure2`, `En_Item00`, `En_Box`,
  `En_Karebaba`, `Obj_Syokudai`, `En_Kanban`, `Door_Shutter` and `En_Md` were read and
  drawn before the rest, which is what added steps, lists of match values and the `scene`
  field. It also found what drawing lacked: segment 1's billboard matrix, which
  `gItemDropDL` multiplies by, now kept there by the viewport as `Play_Draw` keeps it; the
  `LookAt` that `G_TEXTURE_GEN` needs, now a `hilite` step and a frame-wide default; and
  that the harness's images hid what is translucent, since the framebuffer's alpha is
  coverage, not opacity, so they are written opaque.
- **Recorded against a commit**: `checked_against` is the HackerOoT commit the run read.

## Decided

- **Fields are typed**: flag, enum with editable value names, integer.
- **Rotation as params is in the first version**: fields may read a rotation axis, and an
  entry says which axes it is drawn turned by.
- **Parts are in the first version**: a display list may be placed relative to the actor.
- **A variant may name its animation frame.**
- **The shipped file records the HackerOoT commit it was checked against**, and the startup
  check warns about shipped entries that do not fit the checkout.
- **Entries also say**: name, description and notes; colours; billboard; shadow; children;
  spawn conditions; field roles and meanings; default params; collision; lights; text IDs.
- **The curated run comes after step 2**, in one pass.
- **Steps for what fixed keys cannot say**, found by a trial of ten actors: setup display
  lists that differ by list, a variant's own shadow, display lists drawn with a limb,
  skeletons placed and scaled, matches on lists of values and on the scene.
- **Objects are read by the spec**, so `gameplay_keep` takes `gameplay_hacker_keep` as the
  build links it.
- **Link is drawn as a marker** until his pose can be read.

## Later

- **Editing placed actors' params** in the Scene panel, by their fields' controls, written
  back to the scene's C. It is data, not geometry, so it is not excluded by the non-goals,
  but it is its own feature.
- **Flag kinds**: switch and collectible flags, linked to a view of a scene's flags.
- **Naming enum values from the C**, where the guesser finds an `enum` or `#define`s that
  match a table's indices.

## Plan

Each step is committed on its own and ends with its tests passing. S is about a session,
M two or three, L more.

| Step | What | Size |
|---|---|---|
| 0 | Measure Ice Cavern's actors in a release build over the share, split into overlays and objects (done) | S |
| 1 | The database: every key above as types; reading and writing `actors.toml`, with diagnostics naming the actor and key for anything wrong; validation of fields, values and matches; the two files layered; `checked_against` (done; symbols are checked against the objects by step 3's checkout test) | M |
| 2 | Drawing from it: each placed actor's variant picked by its fields and age; turned only by the axes it rotates by, on its frame; display lists placed, coloured and billboarded; shadows; children; guesses only for actors without an entry, turned into entries by `makeGuessedEntry`; the Player entry and the entrance being looked at (done) | L |
| 3 | The curated run: every vanilla actor, in batches by category, validated by a checkout test after each, uncertain ones drawn and looked at (a trial of ten is shipped; the rest waits on its review) | L, in batches |
| 4 | Guesses kept: the on-disk guess cache, and stale guesses by `guessed_from` (`makeGuessedEntry` came with step 2) | S |
| 5 | The startup check: new actors found, the question, guessing them on a worker, writing the checkout's file, the remembered "No"; shipped entries that do not fit, in one warning; watching the checkout's file | S–M |
| 6 | The Scene panel: names, decoded params with roles and meanings, entry status, spawn conditions greying out actors, "Edit actor", "Add a variant for these params", "Keep this guess" | S–M |
| 7 | The Actors panel: the list, the entry editor with typed fields, enum values, roles and meanings, matches edited by the fields' controls, pickers from the object catalog, display list placement and colours, children, edits drawn at once and saved | L |
| 8 | Better guesses: tables indexed by params become enum fields and variants, for display lists, skeletons, scales and segments; one-bit fields become flags; shadows and spawn conditions from `Init` | M |
| 9 | Object reading made cheaper, as step 0's numbers say: parallel reads, or images kept on disk | S–M |

Steps 1 and 2 give a database that works when written by hand, and step 3 writes it for
every vanilla actor. Steps 4 and 5 fill the checkout's file for a mod's own actors without
anyone writing anything. Steps 6 and 7 make it editable without a text editor. Step 8 makes
those guesses better. Steps 0 and 9 are independent of the rest and can be done at any time.
Lights and text IDs are stored by step 1 and filled by step 3, but used by nothing until the
viewport shows lights and a message editor exists.

The format in the examples above is what step 1 builds; everything after it reads and writes
that format, so it is the place to change the design before then.
