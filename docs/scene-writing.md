# Writing scenes

How a scene is saved: its C written again, whole, out of the memory image it was read
into, in the layout fast64 exports, so it can be edited and saved without a build and look
and behave the same in game.

## Written from the bytes, not edited as text

A save never edits the C it read. It writes every declaration again from the image's
bytes: each symbol's type, its bytes as they were before addresses were patched in, and its
relocations, which name the symbol each address points at and keep the constant added to
it in the word ([memory-image.md](memory-image.md)). That is all the C said that the game
sees, so nothing about how the old file was written has to be kept or understood.

What is lost is what the bytes do not hold: comments, formatting, `#define`s and the
branches an `#if` did not take. A save says so when the files it replaces had any, except
an include guard, `#pragma once`, the `LENGTH_<symbol>` defines vanilla headers give
array bounds, and a define whose name starts with a symbol's (`<texture>_WIDTH`), which game
code may use: those are written again, the last kind as its line read, before the symbol's
`extern`. HackerOoT's feature switches are the exception to
the exception: data under `ENABLE_ANIMATED_MATERIALS`, `ENABLE_CUTSCENE_IMPROVEMENTS` or
`ENABLE_MM_TITLE_CARDS` is written under its `#if` again, and a scene whose files hold
data under a switch that is off is not saved, because that data is in no image and would be
lost.

## The layout

A saved scene is laid out as fast64 exports one, which the checkout's own example scene
shows (`assets/scenes/example/`):

| File | Holds |
|---|---|
| `<scene>_scene_main.c` | the scene's headers and the lists they point at |
| `<scene>_scene_col.c` | the collision header and what it points at |
| `<scene>_scene_cs_<n>.c` | each cutscene |
| `<scene>_room_<n>_main.c` | the room's headers and the lists they point at |
| `<scene>_room_<n>_model_info.c` | the room shape and its entries |
| `<scene>_room_<n>_model.c` | display lists, vertices, textures |
| `<scene>_scene.h`, `<scene>_room_<n>.h` | an `extern` for every symbol of the scene, and of each room |

Unlike fast64, each room has its own header, as vanilla's rooms have: game code includes
some of them (`spot16_room_0.h`). The spec lists the files of each segment in that order.

A declaration goes into the file of whatever reaches it first from the segment's header:
the room shape and everything it points at into the model files, the collision header and
what it points at into the collision file, a cutscene into its own file, and the rest into
the main file. A symbol never moves to another segment: a scene's palette its rooms use
stays in the scene.

Every symbol keeps its name and its declared type. What the symbols are called comes from
whoever named them, the extractor, fast64 or the person editing; a new one is named the way
fast64 names it (`<room>_header00_actorList`).

## How each declaration is written

Every declaration can be written without being understood: its type, from the prelude the
file was read against, says how the bytes split into fields, and a relocation in a field
makes it an address of a symbol. Structs are written as positional initializers, a union
as its first member, which in `Gfx`, `Vtx`, `SceneCmd` and `CutsceneData` covers the whole
of it. That writes cutscenes as their words and a display list as `{ w0, w1 }` pairs: exact,
but not readable.

What a person reads is written by typed writers, each checked by encoding what it wrote
and comparing the bytes, falling back to the generic form where they differ:

- scene commands as their `SCENE_CMD_*` macros;
- actor, transition actor and player entries, actor IDs and object IDs by the names of the
  checkout's tables;
- vertices as fast64 writes them;
- collision as fast64 writes it: polygons with `COLPOLY_VTX` and `COLPOLY_SNORMAL` (the
  shortest fraction that truncates to the stored normal), surface types as `SURFACETYPE0` and
  `SURFACETYPE1`, waterboxes with `WATERBOX_PROPERTIES`, and cameras by their setting's name;
- display lists as one `gs*` macro a command;
- textures, palettes and binaries built from a file the build reads (`.png`, `.jpg`,
  `.bin`) as an include of that file again, copied with the scene. fast64 writes textures
  inline, and those stay inline.

The generic writer is `z64::DeclarationWriter`, and a typed writer is an `ElementWriter`
for one element type, handed to it by name. Each element a typed writer writes is encoded
on its own (`z64::makeEncodingCheck`) before it is used, so a writer may guess: a command
whose unused bytes are not zero, or a macro that spells it differently, simply comes out in
the generic form.

A scene command is known by the value of its `SCENE_CMD_ID_*` enumerator under the
checkout's configuration, so HackerOoT's own commands, whose IDs shift with the feature
switches, are written as macros only when the switch that defines them is on, each in an
`#if` of its switch as fast64 writes them (an `ElementCondition`), so the scene still
builds once the switch is turned off. `SCENE_CMD_LIGHT_LIST` names an ID that does not
exist, so the light list is always written as its words.

## Edited as symbols

An open scene is edited as a `SceneModel`: the symbols of the scene's image and each
room's, in their layout order, each with the alignment and file start it was laid out
with. An edit replaces, adds or removes whole symbols (a removed one must have nothing
pointing at it), and the model lays its images out again against the game's code, so the
viewport, the display list viewer and the header readers see an edited scene the way they
see one read from the checkout. Addresses move as symbols grow; relocations follow them.

What the Scene panel edits are the placements of a layer: a room's actors, the scene's
transition actors, and where the player enters (`PlacementEdits`). An edit changes the
bytes of the list that layer's header points at, and the count in the header's command:

- A header another layer reads too is copied for the edited layer first
  (`<room>_header02`), and so is a list another header points at, named as fast64 names one
  (`<room>_header02_actorList`), so an edit of one layer never shows in another. A shared
  main header keeps its place, and the layers that point at it get the copy, without the
  alternate header list, which only a main header holds. A room with no header of its own
  for a layer is edited in its main header, since that is what the game reads for it.
- An actor whose database entry names an object no keep object and no object list of its
  room loads has that object added to the room's object list in the same layer, since the
  game spawns no actor whose object is not loaded. Objects are never removed with an actor:
  actors spawn others whose objects the list holds for them. A room's object list is set
  whole by hand (`setRoomObjects`), to remove or reorder its objects.
- Removing a list's last placement removes the command, and the list once nothing points
  at it. A header with no list for a kind is given one when a placement is added, its
  command before the end. Player entries are set, never added or removed: the spawn list
  and the entrance table name them by index.

The settings a header holds in its commands' own bytes are edited the same way
(`setHeaderSetting`): each is a field of its command, masked, so setting one writes its
bits and no other, in the header the layer reads, made its own first. A header without the
command is given it before its end, its other settings at the values that change nothing
where the game has one (a time of 0xFF leaves the time as it was), zero otherwise.

A layer's light settings and paths are edited the same way, each copied for the layer
first when another layer reads it too (`LightSettingEdits`, `PathEdits`). A light setting is
a list its command counts, as placements are; the last one is not removed. A path list has
no count, and is as long as its array, so a path is added or removed by growing or
shrinking the list, a new path's points made a `Vec3s` array of their own
(`<list>_points<n>`), and a path's points are replaced whole, the count in its `Path` with
them. Points another layer's list shares are copied before they change, and points nothing
points at once a path is removed are removed with it.

An edit that fails changes nothing. Every edit is a step Edit › Undo takes back
(`SceneEditor` keeps the model before each, the last 64), the object an actor needs added
in the same step as the actor; a save, or reading the scene again, starts the history
afresh. While a scene has edits no save wrote, its files read
again, or another scene opened, do not replace it: the panel asks whether to throw the
edits away first. A save takes the edits as written, and a vanilla scene is opened again
from its copy under `assets/`.

## Where it is written

A scene from the checkout's `assets/` is written over its own folder: the new files in
place, the files of its old layout removed, and the spec's segments of the scene and each
room made to include the new objects, keeping their other lines. A vanilla scene, read from
`extracted/<version>/`, is never written there. It is saved to the same path under
`assets/`, its PNGs copied beside it as they are, and the spec segments in
`spec/scenes_*.inc` are pointed at the copy, so the build links it from then on. The asset
list shows the copy in place of the extracted scene.

A save warns, in the plan, about what it cannot keep: the `#define`s and `#if`s it drops, a
display list command that holds an address as a number rather than a symbol (it is written
as the same number, and points elsewhere once what was there moves), and, for a scene
fast64 exported, that Toki owns it once saved: its `.blend` no longer matches, and a
re-export from Blender replaces what Toki changed. A scene Toki wrote carries
`// toki: scene` at the top of its main file and is not warned about. The asset listing
reads that one line of each scene in the checkout's own assets (`isWrittenByToki`), so the
Assets panel shows the scene's source as toki, not fast64.

## Checked before it is written

A save writes the new files into memory, reads them back as the build would, and compares
the images symbol by symbol with the ones it wrote them from: every symbol there, with the
same type, the same bytes before patching, and relocations to the same symbols with the
same constants. Only then is anything written. Layout is not compared, since a file split
differently pads differently, so a vanilla scene saved in fast64's layout passes, and a
scene Toki wrote, saved again, comes out byte for byte the same. Every vanilla scene of the
checkout passes (`SceneSaveCheckoutTest`).
