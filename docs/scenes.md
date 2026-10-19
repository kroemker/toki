# Scenes

How a scene folder becomes something the viewport draws: which files make which images,
how the scene's and rooms' headers are read, what the game does around a room's display
lists that the viewport has to do too, and how a fast64 export shows without a restart.

## Files to images

The game loads a scene's file into segment 2 and each room's file into segment 3, one room
after another. `hackeroot::findSceneFiles` groups a scene folder's C the same way: the
scene's files (`<scene>_scene*.c`) for segment 2 and each room's (`<scene>_room_<n>*.c`) for
segment 3, the file holding the header first, because the game reads the header from the
start of the segment. That grouping stands in for the spec, which is what the build goes
by; it holds for the extracted assets and for fast64's exports, and reading the spec is
still to do.

`toki::scene::readScene` reads them with a `hackeroot::AssetImageReader` as one image per
segment load ([memory-image.md](memory-image.md#several-images)): the game code the scene is
drawn with, shared between reads, then the scene, then each room. Rooms resolve their scene's symbols, the room
lists' own textures included.

## Headers from bytes

A scene's commands are read out of the image, not out of the C. `readSceneHeader` walks the
`SceneCmd`s from the start of segment 2 the way `Scene_ExecuteCommands` does, eight bytes
at a time until `SCENE_CMD_END`, and follows each pointer into the image's bytes:

- the room list's count;
- the player entry list, an `ActorEntry` each, which is where the camera starts;
- the transition actor list, a `TransitionActorEntry` each: doors and loading planes, with
  the room on either side;
- the light settings list, an `EnvLightSettings` each, with the fog near value unpacked
  from the blend rate it shares a half with;
- the skybox settings' light mode, which says whether the time of day picks the setting;
- the special files' keep object, which the game loads into segment 5 for the whole scene
  (`gameplay_field_keep`, `gameplay_dangeon_keep`);
- the alternate header list.

`readRoomHeader` does the same from the start of segment 3: the room shape, of any of the
three types (normal, image, cullable), as each entry's opaque and translucent display list,
the actor list, the object list (the objects loaded for the room's actors, by ID), and the
room's own alternate header list.

Reading the bytes rather than the C is the same decision as the memory image itself: the
bytes are byte-identical to what the build makes, so every macro, cast and initializer
quirk in a header has already been answered the way the game will see it. The command IDs
0x00 to 0x19 are the game's own, fixed by the order of its command table. HackerOoT's
additions come after them and are not read yet.

## Layers

The game's scene layer picks Link's age and the time of day (layers 0 to 3) or a cutscene
(4 on). Layer 0 is the main header; layer `n` is entry `n - 1` of the main header's
alternate header list, a full header of its own. An alternate header list holds no count:
the game only ever reads the entry its layer asks for. Its length is taken from the array
the image laid out at its address, which is how many entries the C wrote. A room follows its
scene's layer the same way, and where a room's list has no entry for a layer, the room's
main header stands, as `Scene_CommandAlternateHeaderList` carries on with the main header
when the entry is null. `readScene` reads every layer that has a scene header of its own.

A pointer that runs off the image, a header without an end, and a room without a shape are
diagnostics naming the address, never a failed read.

## Drawing it as the game does

A room's display lists assume what `Play_Draw` and `Room_Draw` set up before calling them.
The viewport sets up the same, from the checkout's own code where the game has it as data:

- **The view.** The projection matrix is the projection times the view, and the modelview
  is the identity, as `View_Apply` and `Room_Draw` leave them. A room that loads its own
  modelview matrices then draws where the game draws it.
- **Lights and fog**, from the scene's current light setting, as `Lights_BindAll` and
  `Play_SetFog` set them: the ambient colour and the two directional lights, the fog colour,
  and `gSPFogPosition(fogNear, 1000)`. The setting's `zFar` is the view's far plane. A scene
  opens at midday where the time of day picks the setting, at its first setting otherwise.
- **The draw config.** `sDefaultDisplayList` from `z_scene_table.c` points segments 8 to 13
  at `gEmptyDL` and sets the primitive and environment colours. Scenes with a draw config
  of their own (texture scrolls, Deku Tree's day and night entrance) run code to set those
  segments, which the viewport does not run. After the default, segments 8 to 13 are pointed
  at one stand-in (`scene::makeRuntimeSegments`): an identity matrix followed by
  `gsSPEndDisplayList`. An identity `Mtx` reads as `G_NOOP`s, so a room that calls one of
  those segments returns at once, as with `gEmptyDL`, and one that multiplies by a matrix
  in one is drawn unmoved. Jabu-Jabu's rooms multiply by the matrix its draw config scales
  them with every frame, in segment 13; `gEmptyDL` read as that matrix threw their vertices
  across the screen. A texture read through one of those segments is a diagnostic.
- **The setup display list.** Each pass starts with `sSetupDL[SETUPDL_25]` from `z_rcp.c`,
  as `Gfx_SetupDL_25Opa` and `Gfx_SetupDL_25Xlu` call it: two-cycle mode, bilinear
  filtering, the fog render mode, lighting on. Rooms change only what differs from it.
- **The passes.** Every room's opaque display lists are drawn, then every room's
  translucent ones, as the game draws all of `POLY_OPA` before `POLY_XLU`.

The code files are read into segment 0, which maps at physical address 0, so the code's own
addresses (`gEmptyDL` in `sDefaultDisplayList`) are physical, as the game's are.

## Showing it

How a scene is shown is a `toki::scene::SceneView`: the layer, the light setting, which
rooms, and whether placed actors are marked. `makeSceneContent` turns a scene and a view into
what the viewport draws, without reading anything again: the images are shared, immutable,
between the scene and every content made from it, so changing the layer or the light only
makes a new list of draws.

Placed actors and transition actors whose overlay says how they are drawn are drawn as
their models, after the rooms of each pass ([actor-rendering.md](actor-rendering.md)).
The rest, and player entries, are marked by a small shape each, coloured by kind, from a
display list built by hand into segment 15 (`makeActorMarkers`), drawn between the opaque
and the translucent pass. F3DEX2 draws no lines, so a marker's edges are thin dark boxes;
each marker is drawn under its own matrix, scaling down vertices written at 1/32 of a world
unit, so an edge can be thinner than the unit positions are given in. The shape is a setting
(Viewport › Actor Markers): a cube by default, a diamond, or a pin, a pyramid on its tip
that points at the exact spot an actor is placed.

The Scene panel is the view's controls for what is shown: the layer, and a tree of the layer
(`SceneOutliner`). Under the scene are its rooms, each with an eye that hides it, its actors
by name from the checkout's actor table, with how each is drawn and its params, and the
objects its object list loads; then the transition actors, the entrances, and the light
settings, a click on one lighting the scene with it. Collision, paths, cutscenes and exits
are rows that say they are not read yet. A filter keeps the placements whose name, enum name
or database name holds it. A placement selected elsewhere, in the viewport, opens its group
and is scrolled to.
How it is drawn is chosen in the viewport's bar: the scene offers its light settings and how
its actors are shown (hidden, marked, or drawn as their models) there as
`viewport::ViewportChoices`, and applies what is picked to its view. The viewport knows no
scene; it only shows the rows it was offered. Choosing an actor
turns the camera to it. A scene read again keeps its view, kept within what the scene now
has; opening another starts afresh.

A room or an actor is selected by its row or by clicking anything it draws in the viewport.
Every draw of the scene's content is owned by the node it is part of, a room's mesh by its
room and an actor's models, its children's and its marker by its placement, as a number
(`makeNodeKey`), and the click picks the owner of the nearest triangle the frame drew under
the mouse, so it is what the game drew, not a bounding box. A selected room is shown
read-only: its shape, parts, actors and objects.

The Properties panel, below it, shows the placement selected in the panel or picked in the
viewport, in three sections:

- **Actor** is the placement as the game stores it: its type, position, rotation and params,
  raw. A rotation is shown in binary angles or degrees (Scenes › Rotations In, or the button
  beside it), except an axis the actor reads params from, which is always shown raw.
  Position and rotation are dragged, a unit or a degree a point, or typed after a
  double-click; a dragged angle wraps at a full turn. While a field is held the viewport
  shows the placement as it now is (`findChangedDraft`), drawn from a copy of the layer as a
  transform's preview is, and the edit is made, one undo step, when the field is let go.
- **Params** is the same bits as the actor's database entry names them: a flag as a
  checkbox, a number as a field, a choice as a dropdown of its values or its collection's
  (`ParamsForm`). Each control writes only its own bits (`writeParamValue`), and the raw
  params above are always what is stored, so they override whatever the entry says. Bits no
  param names are shown as Other Bits when the placement sets any, and the variant the params
  match is named. An actor without an entry, or an entry without params, has only the raw
  field.
- **Object** is the object the variant is drawn from, and whether the room loads it in this
  layer, with a button to add it to the room's object list when it does not.

Its Flags section lists every scene flag the layer's placed actors use, by the params their
entries mark as one ([actor-database.md](actor-database.md)), in the rooms and between them
(`listSceneFlags`): each flag by kind and number, whether it is saved or reset when the
scene is left, and the actors that read or set it, those more than one actor uses first. A
flag at its param's highest value means none, as actors take it, and is left out. A flag
read but set by no actor here says so, since a cutscene, another scene or the player may set
it. Choosing an actor turns the camera to it.

The viewport links them: a line from each actor that sets a flag to each that reads it,
headed at the reader, and at both ends when each does both (`listFlagLinks`), coloured by
kind and raised a little above the placements. Hovering a flag in the list dims every other
flag's lines. They are handed to the viewport as a `ViewportOverlay` and drawn in its overlay
pass ([renderer.md](renderer.md)), not by F3DEX2, which has no line: tested against the
frame's depth, so a wall hides the part of a link behind it, and with X-ray on that part
is drawn faintly, to follow a switch through the wall to its door. A line passing behind
the camera is cut at the near plane, and an arrowhead lies flat on the screen at the depth
of the end it points at (`makeArrowLines`). Actors in hidden rooms are not linked, and the
viewport bar's Flag Links overlay hides the lines.

The scene open when the app closes is saved in `config.toml` under `[session]`, with its
view and the camera, and opened again once the checkout has been read. The layer is saved as
the game's layer number rather than a position in the scene's list, so it means the same
layer after the scene gains or loses one; anything the scene no longer has falls back as
`keepSceneView` keeps a view. A folder that is gone is a warning in the log.

## Reloading

`toki::scene::SceneLoader` reads the open scene on a worker thread and polls its files'
modification times, and the folder's, every half second
([workspace.md](workspace.md) explains why polling and not notifications). A change reads
the scene again and the viewport replaces what it draws, keeping its camera. The watch is
set before the read starts, so a save during a read is not missed.

## Measured

Against a HackerOoT checkout over `\\wsl.localhost`, release build, the scene and its rooms
together with the two code files:

| Scene | Rooms | First read | Read again |
|---|---|---|---|
| spot04, Kokiri Forest | 3 | 4.1 s | 1.9 s |
| ydan, Deku Tree | 12 | 4.7 s | 2.3 s |
| example, fast64 | 1 | 2.5 s | 0.4 s |

A read again is what a reload costs: the preludes are kept, so what is left is reading the
files and encoding them. For a vanilla scene that is mostly its display lists, every one
expanded through the checkout's `gbi.h`; a debug build takes several times longer. The two
code files are read again on every reload too, and are the next thing to keep.
