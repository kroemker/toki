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
drawn with, then the scene, then each room. Rooms resolve their scene's symbols, the room
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
- the alternate header list.

`readRoomHeader` does the same from the start of segment 3: the room shape, of any of the
three types (normal, image, cullable), as each entry's opaque and translucent display list,
the actor list, and the room's own alternate header list.

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
  segments, which the viewport does not run: they are drawn with the default, and a texture
  read through one of those segments is a diagnostic.
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

Placed actors, player entries and transition actors are marked by a small diamond each,
coloured by kind, from a display list built by hand into segment 15 (`makeActorMarkers`),
drawn between the opaque and the translucent pass. Drawing an actor's own object instead is
the actor render database, still to come.

The Scene panel is the view's controls: the layer and light pickers, a checkbox for each
room, and each room's actors by name from the checkout's actor table. Choosing an actor
turns the camera to it. A scene read again keeps its view, kept within what the scene now
has; opening another starts afresh.

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
