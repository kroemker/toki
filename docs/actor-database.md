# The actor database

How each actor is shown in the viewport: a database per checkout, edited in the application,
seeded from Toki's own and filled in for the rest by reading each actor's C (see
[actor-rendering.md](actor-rendering.md)). This document describes format 2 and the editor
built on it, all of which is built ([the plan](#plan)).

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
is where that is written. Reading the C stays, as what fills an entry in so nobody starts
from nothing.

## What it is, and is not

- **It changes how Toki shows actors, never the checkout's code.** The panel that edits it
  is called "Actor Database" and says so at its top.
- **It is the source of truth.** An actor with an entry is drawn as its entry says. Its C is
  read only when the entry is derived.
- **An entry is there or it is not.** There is no status, nothing marks an entry as guessed
  or checked, and an actor without an entry is drawn as a marker.
- **It shows where an actor is and what it is, not everything it does.** Actors do far more
  when they draw than any format could say, so the line is drawn at what helps place one:
  display lists and skeletons, placed, coloured and posed. Animation, shadows, what is drawn
  with a skeleton's limbs, spawn conditions, lights and collision are left out.
- **Actor code is still never run.**

## One file per checkout

The database lives in the checkout, at `<checkout>/.toki/actors.toml`. `.toki/` is where
Toki keeps what it knows about a checkout, and where more of it can go later; it is
committed with the mod and shared by everyone who works on it.

- **Seeded.** The first time a checkout without the file is opened, Toki writes its own
  database there (`assets/actors.toml`, installed beside the executable), then derives every
  actor the actor table lists that it does not cover, on a worker, and saves the result.
  From then on only the checkout's file is read; a later Toki with a better database of its
  own does not change it. A checkout is not seeded while Toki's own database is missing or
  does not read cleanly, nor while whether the file exists cannot be told: seeding over a
  file that is there would replace it. Deriving an entry again picks up what a newer Toki knows.
- **Derived.** "Derive" fills an entry from what Toki knows: its own entry for the actor
  when it has one, and otherwise what reading the actor's C finds. Deriving an actor that
  has an entry asks first. Showing what would change is open.
- **New actors.** At startup and whenever the actor table changes, the table is checked
  against the file. Actors it lists that have no entry are offered: "N new actors were
  found. Derive them?" No is remembered in `config.toml` (`actors.dismissed_new`) until the
  set of new actors changes, or another checkout is picked.
- **Watched.** The file is polled like the asset files are, so editing it by hand outside
  Toki redraws the open scene.

## An entry

An entry is keyed by the actor's enum name, `ACTOR_EN_BOX`, not its ID: an ID moves when an
`#if` in the actor table changes, the name does not.

```toml
format = 2

[actors.ACTOR_EN_BOX]
name = "Treasure chest"
notes = "Lens chests are drawn as always seen."
object = "object_box"
scale = 0.01
rotates = { x = true, y = true, z = false }

[actors.ACTOR_EN_BOX.params]
"Chest Flag" = { mask = 0x001F, kind = "number" }
"Type"       = { mask = 0xF000, kind = "choice", values = { 0 = "Big", 2 = "Boss Key", 5 = "Small", 7 = "Small, appears on room clear" } }
"Switch"     = { mask = 0x003F, source = "rotation_z", kind = "number", description = "Makes a switch chest appear" }

[[actors.ACTOR_EN_BOX.variants]]
match = { "Type" = [5, 7] }
scale = 0.005
parts = [
    { display_list = "gTreasureChestChestFrontDL", rotation = [0, 180, 0], env = [0, 0, 0, 255] },
    { display_list = "gTreasureChestChestSideAndLidDL", rotation = [0, 180, 0], env = [0, 0, 0, 255] },
]

[[actors.ACTOR_EN_BOX.variants]]
parts = [
    { display_list = "gTreasureChestChestFrontDL", rotation = [0, 180, 0], env = [0, 0, 0, 255] },
    { display_list = "gTreasureChestChestSideAndLidDL", rotation = [0, 180, 0], env = [0, 0, 0, 255] },
]

[actors.ACTOR_EN_MD]
name = "Mido"
object = "object_md"
scale = 0.01

[[actors.ACTOR_EN_MD.variants]]
parts = [{ skeleton = "gMidoSkel", pose = "gMidoIdleAnim", segments = { 8 = "gMidoEyeOpenTex" } }]
```

- **`name`** is what the panels call the actor; **`notes`** is anything a person wants to
  say about it.
- **`object`, `scale`, `height_offset`** apply to every variant that does not set its own.
  `height_offset` raises the model by that much times the scale, as `Actor_Draw` does.
- **`rotates`** says which of the placed rotation's axes the actor is drawn turned by, all
  three when left out. An actor that reads params from its rotation usually draws unturned
  on those axes.
- **`draw`** is `marker` for an actor drawn as a marker by design, and `children` for one
  that draws nothing itself but places others.
- **`params`** name the bits of its params ([Params](#params)).
- **`variants`** are tried in order ([Variants](#variants)).

### Params

A param is a named range of bits of the placed actor's params, or of one axis of its placed
rotation, which some actors read more params from. Its name is free text, as people say it:
"Drop Table", "Switch Flag".

| Key | Means |
|---|---|
| `mask` | its bits: adjacent ones only, so the shift and width follow from it |
| `source` | `params` (left out), `rotation_x`, `rotation_y` or `rotation_z` |
| `kind` | `flag`, one bit shown as a checkbox; `number`; or `choice`, one of its `values` |
| `values` | for a choice, each number it may be and the label shown for it; or the name of a collection of the checkout to take them from, `"GetItemID"` (see below) |
| `offset` | added to the param's value before it is looked up among its values, as a sign adds `0x300` to its message |
| `signed`, `hex` | a number read as signed, or shown in hexadecimal |
| `description` | a line on what it is for |

A mask whose bits are not adjacent, two params of one source that share a bit, a choice
value that does not fit the mask, and a param named `age` or `scene` are each a warning
naming the actor and the param.

### Value collections

A choice whose values the checkout already lists names them instead of copying them, so
they follow the checkout and never drift: `values = "GetItemID"`. Each value is labelled
with its constant's name, `GI_HEART_PIECE`, since that is what modders search the code
for; the file still holds numbers, in matches and `default_params` alike.

- **Shared collections** every actor can choose from: `GetItemID`, `ItemID` and
  `GetItemDrawID` from `include/item.h`, `Item00Type` from `include/z_en_item00.h`, sound
  effects `SfxId` from `include/sfx.h`, music `NA_BGM` from `include/sequence.h`, and
  `ActorID`, `ObjectID`, `SceneID` and `EntranceIndex` from the tables the workspace reads.
  Constants ending in `_BASE`, `_END` or `_MAX` bound an enumeration rather than name a
  value, and are left out.
- **An actor's own** are the enumerations its overlay headers define, `EnBoxType`; a name
  is looked up among the actor's own first.

The headers are read as the build compiles them (`hackeroot::readPrelude`), on a worker
(`actor::CollectionCatalog`), the shared ones when a checkout is opened and an actor's the
first time it is asked for, and read again when one of their files changes. A collection
the checkout does not have, one whose values mostly do not fit the param's bits once its
offset is taken off (a sentinel such as `ITEM00_NONE` is not the wrong collection), and
matches on values the collection lacks are warnings in the panel
(`actor::checkCollections`); a collection is not a warning while it is still being read.

### Variants

```toml
[[actors.ACTOR_OBJ_SYOKUDAI.variants]]
match = { "Type" = 0, "Lit" = true }
parts = [
    { display_list = "gGoldenTorchDL" },
    { display_list = "gEffFire1DL", object = "gameplay_keep", pass = "translucent", translation = [0, 52, 0], scale = 0.0027, billboard = true, prim = [255, 255, 0, 255], env = [255, 0, 0, 0] },
]
```

- **`match`** lists params and the value each must have, a number or a list of numbers any
  of which will do; a flag's is `true` or `false`. A choice is matched by its numbers, so
  its labels can change freely. `age` (`child` or `adult`, from the scene layer) and `scene`
  (the scene's enum name from the scene table, `SCENE_DEKU_TREE`) are matched like params
  every actor has. A variant without `match` matches every placement, and belongs last.
- **`object`, `scale`, `height_offset`, `draw`** override the entry's.
- **`parts`** are what it draws ([Parts](#parts)).
- **`children`** are actors it spawns, each by its enum name, its params and its offset from
  the parent, turned with it, and drawn by its own entry. Children of children are not
  followed.
- **`default_params`** are what its params are when the actor is placed new.

### Parts

Each part draws one display list or one skeleton, and says everything it is drawn with;
nothing carries over from one part to the next.

| Key | Means |
|---|---|
| `display_list` or `skeleton` | what it draws, by name |
| `object` | where the name is, when not the variant's |
| `pose`, `pose_object` | for a skeleton, the animation it stands in, at its first frame; at rest without one |
| `translation`, `rotation`, `scale` | where it is relative to the actor: moved, turned in degrees about Z, then Y, then X, and scaled |
| `billboard` | it faces the camera, as flames and sprites do |
| `prim`, `env` | the primitive and environment colours it is drawn in |
| `segments` | segments 8 to 12 pointed at symbols, eyes and mouths; the others point at `gEmptyDL` |
| `setup` | the `sSetupDL` it is drawn after, `SETUPDL_25` when left out |
| `pass` | `opaque` (left out) or `translucent` |
| `hilite` | `func_8002EBCC`'s `LookAt`, from the camera toward the actor, for what shines by `G_TEXTURE_GEN` |

A part without `hilite` is drawn with the frame's `LookAt`, from the camera toward what it
looks at, standing in for whatever the game drew last.

### Player

Player has no overlay, so Toki's own entry for it is written by hand. Which model Link has
depends on his age, so his variants match on `age`. He is drawn as a marker until his pose
can be read: his animations live in `link_animetion`, which is not asset C, and at rest his
limbs lie apart. He is drawn at the entrance the camera is looking at, not at every player
entry of the scene at once.

## Deriving

The rules of [actor-rendering.md](actor-rendering.md) read an overlay's `Init` and `Draw`
for literals: the object, the scale, a skeleton and the animation it starts in, display
lists, and the segments `Draw` points. `makeGuessedEntry` turns what they find into an
entry with one variant. A table indexed by params is today a gap; later, it becomes a param
and one variant per entry of the table ([Later](#later)).

## Editing

- **The Scene panel** shows each placed actor's params as its entry reads them. "Edit actor"
  opens its entry, and "Add a variant for these params" adds a variant matching exactly them,
  first, drawn as the variant they match now.
- **The Actor Database panel** lists every actor of the actor table, whether it has an entry,
  filtered by name, and edits one entry. Every row is a label on the left and its input on
  the right; numbers are typed, and dragged only where it helps.
  - **Params** are shown as one strip of 16 bits per source used, each param in its own
    colour. Clicking a bit gives it to the param being edited, or takes it back; bits another
    param has are not clickable and say whose they are. Each param has its name, kind,
    description, sign and hexadecimal options, and a choice its values and their labels, or
    the collection it takes them from, picked from the actor's own and the shared ones, and
    its offset. A condition on a choice is a searchable list of its values to tick; on the
    scene, of the scene table's.
  - **Variants** are cards, headed by their condition, "1. Type is Golden, and Lit is
    set", or "Otherwise" for a last one that matches everything. A card has bands down a
    labelled left gutter, so what belongs to what shows without deep nesting:
    - **WHEN**, tinted, holds the condition as chips that wrap, "Type  Golden", each opening
      the param's own control, a checkbox, the choice's values or a list of numbers.
    - **DRAWS** holds a line of the variant's object, scale and the rest, then one row per
      part: a tag for its kind (DL, SK), its symbol, its own object when it names one, and
      badges for what is set, "faces cam", "xlu", "shiny", "setup 66".
    - **SPAWNS**, when there are children, holds one row per child the same way.

    Rows never wrap: what does not fit is cut with "…" and shown whole on hover. Clicking a
    row opens its properties below it, labels on the left as everywhere else. A row's tools,
    up, copy and remove, show while it is hovered.
  - Objects are picked from the object table, and skeletons, animations and display lists
    from the object's catalog, read on a worker (`actor::SymbolCatalog`).
- **Edits draw** in the open scene once they are saved, a moment after the last change.
  `actor::DatabaseStore` makes every edit on a worker: it reads the file as it is, changes
  it, and does not write one that does not read cleanly, since that would drop what could not
  be read. What the checks find wrong with an entry (a param without bits, a match without
  values, bits that are not next to each other) is kept as written and does not stop a
  write: an entry half edited is often wrong for a moment. Each edit goes to the file it was
  asked for, so a change made just before another checkout is opened lands in the first. The file is written with a stable order of actors and keys and params in
  hexadecimal, so it diffs well, beside itself first and then over itself, so a failed write
  leaves it as it was. Comments in it are not kept.

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

So the overlays are not the cost; the objects are. In the order they would help:

- **One image cache for the session**, shared by every loader, so `gameplay_keep` and every
  object is read once however it is first opened (`hackeroot::ImageCache`, done: a second
  reader reads Ice Cavern's actors in 0.94 s).
- **Images kept on disk**, keyed by their files' modification times, so a second session
  opens a scene's actors in about the second read's time.
- **Objects read in parallel**: the share's cost is latency, not bandwidth.
- **Only the objects of the variants a scene places**, rather than of every variant of every
  actor it places.

## Toki's own database

Toki's own database, `assets/actors.toml` in Toki's repository, is written by reading each
vanilla overlay and its objects by hand rather than by the rules, because a person, or
Claude, reading the C can follow a `switch` on params where the rules cannot. It is what
every checkout is seeded from.

- **In batches by category**: enemies, NPCs, props, background actors, items, effects and
  cutscene actors. The file itself is the progress.
- **Checked by code**: a checkout test reads it and fails on any entry whose objects or
  symbols the vanilla checkout does not have, or whose params overlap.
- **Checked by eye**: the hidden GPU test "every variant of the shipped actor database is
  drawn to an image" (`toki_gpu_tests "[.render]"`, against a checkout) draws each variant
  of every entry by itself, framed on its bounds and its children's, to
  `actor-renders/<ACTOR>_<variant>.png` beside the test binary, and fails on any diagnostic.
- **A trial of ten first**: `Obj_Tsubo`, `En_Kusa`, `Obj_Mure2`, `En_Item00`, `En_Box`,
  `En_Karebaba`, `Obj_Syokudai`, `En_Kanban`, `Door_Shutter` and `En_Md`. They found what
  format 1 could not say, which format 2 settles, and what drawing lacked: segment 1's
  billboard matrix, which `gItemDropDL` multiplies by, now kept there by the viewport as
  `Play_Draw` keeps it; the `LookAt` that `G_TEXTURE_GEN` needs; and that the harness's
  images hid what is translucent, since the framebuffer's alpha is coverage, so they are
  written opaque.
- **Recorded against a commit**: `checked_against` is the HackerOoT commit it was read
  against.

## Decided

- **One file per checkout**, in `.toki/`, seeded from Toki's own and then the checkout's to
  change. No status; "Derive" in place of guesses.
- **Params by mask**, named in free text; choices matched by number, labelled in free text.
- **Parts**, each self-contained, in place of display list keys and steps; a skeleton is
  posed at the first frame of an animation, never played.
- **Left out**: animation playback, limb drawing, shadows, spawn conditions, lights,
  collision, text IDs, roles of params, descriptions of entries.
- **Variants edited as cards** of a condition, the parts and the children, each a row that
  opens in place, rather than as a text language, as nested forms or as sentences of
  tokens, which read well but wrapped badly once names grew long.
- **Objects are read by the spec**, so `gameplay_keep` takes `gameplay_hacker_keep` as the
  build links it.
- **Link is drawn as a marker** until his pose can be read.

## Later

- **Better derivation**: a table indexed by params becomes a choice param and a variant per
  entry, for display lists, skeletons, scales and segments; a one-bit param becomes a flag.
  Toki's ten curated entries are what a derivation is scored against.
- **Editing placed actors' params** in the Scene panel, by their params' controls, written
  back to the scene's C.
- **Choice labels from the C**, where an `enum` or `#define`s match a table's indices.
- **Object reading made cheaper**, as [Cost](#cost) says.
- **The rest of Toki's own database**, every vanilla actor.

## Plan

Each step is committed on its own and ends with its tests passing.

| Step | What |
|---|---|
| 1 | Cut what drawing and the Scene panel leave out: animation playback, so a skeleton is posed at its animation's first frame; shadows; what is drawn with a skeleton's limbs; spawn conditions greying actors; "Keep this guess" (done; the format's keys for them go with step 2) |
| 2 | Format 2: params by mask with choices by number and free-text names, parts in place of display list keys and steps, warnings for masks and overlaps; drawing by parts; the derivation writing parts; Toki's database rewritten (done; until step 4 the Actor Database panel shows an entry as its text) |
| 3 | One file per checkout in `.toki/`, seeded and derived for new actors on a worker; "Derive"; the new-actor question; no drawing by guesses on the fly (done; seeding derives every actor of a vanilla checkout in 0.9 s locally) |
| 4 | The editor laid out as label and input, typed numbers, capitalized options, renamed "Actor Database" with its line at the top (done: the general keys; params and variants are shown as text until steps 5 and 6) |
| 5 | Params edited on bit strips (done) |
| 6 | Variants edited as cards of WHEN, DRAWS and SPAWNS bands, with rows that open in place (done) |
