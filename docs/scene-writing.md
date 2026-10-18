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
an include guard, `#pragma once`, and the `LENGTH_<symbol>` defines vanilla headers give
array bounds, which are written again. HackerOoT's feature switches are the exception to
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

## Checked before it is written

A save writes the new files into memory, reads them back as the build would, and compares
the images symbol by symbol with the ones it wrote them from: every symbol there, with the
same type, the same bytes before patching, and relocations to the same symbols with the
same constants. Only then is anything written. Layout is not compared, since a file split
differently pads differently, so a vanilla scene saved in fast64's layout passes, and a
scene Toki wrote, saved again, comes out byte for byte the same.
