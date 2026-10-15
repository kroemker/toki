---
name: actor-entries
description: Write or fix entries of Toki's own actor database (assets/actors.toml) by reading each vanilla actor's overlay C by hand, in batches, and check them against a checkout. Use when asked to add actors to the database, run the full database pass, or fix how an actor is drawn.
---

# Writing actor database entries

Toki's own database, `assets/actors.toml`, says how each vanilla actor is drawn. Its format is
`docs/actor-database.md`; read it first, it is the reference for every key named here. Each
entry is written by reading the actor's overlay C, never by running it, and checked by the
tests and by looking at what it draws.

## Before starting

- A HackerOoT checkout readable from Windows, with `ENABLE_F3DEX3` false in
  `include/config/config_graphics.h`: Toki refuses an F3DEX3 checkout, and every checkout test
  skips. The build's own objects in `build/` should be from the same source, or the byte
  comparison tests fail for reasons unrelated to the database.
- The release preset configured with `-DTOKI_HACKEROOT=<that checkout>`, and `toki_tests` and
  `toki_gpu_tests` built.
- Which actors: the actor table's `ACTOR_*` names without an entry, or the ones asked for.
  `include/tables/actor_table.h` lists them; `[actors.ACTOR_X]` headers list what is done.

## One actor

1. **Find the code.** `src/overlays/actors/ovl_<Name>/z_<name>.c` and its header. Read the
   profile (its object), `Init` and the setup functions it calls, `Draw` and what it calls,
   and the limb callbacks. Note every symbol from the object's header in `assets/objects/` or
   `extracted/<version>/assets/objects/`; never write a symbol that is not declared there.
2. **Params.** Each `PARAMS_GET_U(params, shift, width)` or mask is a param: its mask, a name
   as a person says it, its kind. A choice names a collection when the checkout has one (an
   enum of the overlay's header or C, a table `sX` of constants, or a shared one such as
   `GetItemID`) rather than listing values. Scene flags get `scene_flag` and `flag_use`. Params
   read from the rotation get `source` and usually `rotates = false` on that axis.
3. **Scale and height.** Always write `scale`: left out it is 0.01, the game's default, which
   is easy to miss. `Actor_SetScale` and `ICHAIN_VEC3F_DIV1000(scale, ...)` set it.
   `shape.yOffset` is `height_offset`; a `world.pos.y += k` in `Init` is `height_offset` of
   k / scale. A translation in a part is in model units: world units / scale.
4. **Variants and adjustments.** One variant per thing `Draw` draws differently. Where params
   pick independent things (a scale from one table, a colour from another, a texture by size),
   write one variant and an adjustment per choice; never multiply them into variants.
5. **Parts**, for each draw call, in order:
   - `gSPDisplayList` is a `display_list`; `SkelAnime_Draw*` a `skeleton` with the `pose` its
     setup picks, and `pose_frame` when the animation is held at a frame.
   - `GetItem_Draw(play, GID_X)` is `get_item = "GID_X"`, or `{ param, table }` when a table
     indexed by a param picks it. Never write an item's display lists out by hand.
   - `Gfx_SetupDL_<n>Opa/Xlu` is `setup = n` unless 25, and `POLY_XLU_DISP` is
     `pass = "translucent"`. `func_8002EBCC` before drawing is `hilite = true`.
     `Matrix_ReplaceRotation(&play->billboardMtxF)` is `billboard = true`.
   - `gDPSetPrimColor` and `gDPSetEnvColor` with literals are `prim` and `env`.
   - `gSPSegment(..., 0x08..0x0C, SEGMENTED_TO_VIRTUAL(tex))` is a segment set to the symbol;
     one set to `Gfx_EnvColor`, `Gfx_TwoTexScroll` or the actor's own builder is a built list,
     `{ env = [...] }` or `{ tiles = [...] }`, scrolls at their first frame.
   - A built list that also calls `gDPSetRenderMode` takes `render_mode = ["G_RM_PASS",
     "G_RM_ZB_CLD_SURF2"]`, its two cycles as `gbi.h` names them.
   - `gSPSegment` to matrices Draw or a limb callback computes (`MATRIX_TO_MTX` into a
     `GRAPH_ALLOC`ed `Mtx` array) is `{ matrices = [...], limb = n }`: each matrix a placement
     relative to the actor, or to limb n when the callback builds them from that limb's
     matrix; write them as they are at rest, with run-time angles at 0.
   - `gSPSegment` to a list of the game's code is `{ code = "D_80116280", index = 2 }`, the
     index counting commands: `&D_80116280[2]` while opaque, the start while translucent.
   - Matrix calls before a draw become `translation`, `rotation` and `scale`. Rotation turns
     about Z, then Y, then X; a game order that differs is converted to the angles that come
     to the same.
   - A display list an overlay defines itself is drawn from `object = "ovl_<Name>"`.
6. **Limbs.** An `OverrideLimbDraw` that sets `*dList` swaps a limb: `limbs = { n = "DL" }`, with
   n the `limbIndex` the code compares against, unchanged; `*dList = NULL` is `n = false`; a
   display list from another object is `{ display_list, object }`. A `PostLimbDraw` that draws
   is a display list part with `limb = n` and the matrix calls before it as its placement.
7. **Children.** `Actor_Spawn` or `Actor_SpawnAsChild` with literal offsets in `Init` are
   `children`; params passed from the parent's are `take`. A spawner that draws nothing itself
   is `draw = "children"`.
8. **Markers.** An actor that draws nothing, or only in debug builds, is `draw = "marker"`.
9. **Notes.** Say what the actor is in a sentence, then what is not drawn and why, and what
   decides whether it appears. Notes are where gaps are recorded, not left out.

## Checking

Run all three, and look:

```
toki_tests "[database]"
toki_tests "a real checkout's collections are read without a diagnostic"
TOKI_RENDER_ACTORS=EN_KO,EN_SW toki_gpu_tests "[.render]"
```

The render test writes `actor-renders/<ACTOR>_<variant>.png`, `_a<n>` for adjustments and
`_i<value>` for items picked by a param, beside the test binary, and fails on any diagnostic.
Open every image of the actors written: wrong colours, missing limbs, a model at the wrong
size or turned are what the tests cannot see. An image that is black may be a black model on
the black background: check by changing its colour for one run.

## A batch, or the full pass

- Split the actors by category (enemies, NPCs, props, background actors, items, effects,
  cutscene actors), about twenty to a batch, smaller for bosses and NPCs.
- With subagents, give each a batch and this skill, and have it write its entries to its own
  file in the scratchpad, `batch-<n>.toml`, and return what it could not express. Never let
  two agents edit `assets/actors.toml`.
- Merge the fragments into `assets/actors.toml` yourself, run the checks once over the batches
  merged, look at the images, and fix what is wrong before the next round.
- Keep a list of what the format could not say across all batches; it is what the next format
  change is planned from.
