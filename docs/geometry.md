# Geometry

How the rooms' meshes are read, picked and edited: vertices moved in their `Vtx`, faces
taken out, added and moved by rebuilding the runs of the display lists that draw them, and
the materials faces are drawn under listed across rooms. A save writes the result as it
writes every symbol ([scene-writing.md](scene-writing.md)).

Once Toki saves a scene it owns it, geometry included: a re-export from Blender replaces it.

## Reading the mesh

`readSceneMesh` runs each room's shape the way the viewport draws it, seen from nowhere so
nothing is culled, with the RSP recording where every vertex came from. A triangle's corners
are `Vtx` of the scene's images (`MeshEntry`: the array, its index, the whole 16 bytes and
the modelview it was loaded under). A triangle whose corners are not in a `Vtx` array of the
scene is left out with a diagnostic.

Entries are grouped into vertices the way Blender groups them: entries at one place that
are corners of faces meeting along an edge are one vertex, so a seam where texture
coordinates or colours differ still moves as one point. A grouping, once read, is kept
(`kept`) across rereads after an edit, so a vertex picked stays picked.

Each face knows the command that drew it (`MeshDraw`: its `Gfx` array and command index),
whether it was lit, and what it was drawn under (`MeshMaterial`, below).

## Moving vertices

A transform writes each `Vtx` a moved vertex stands for back through the inverse of its
modelview (`makeMeshMoveWrites`). Nothing else is written but what the move makes stale:

- a lit vertex's normal, made again from the faces around it;
- each cullable part's bounding sphere (`RoomShapeCullableEntry`), so a part moved out of
  its old sphere is not culled;
- with the link toggle on, the collision vertices that stood where the mesh vertices stood,
  and the polygons they make faced and bounded again ([collision.md](collision.md)).

## Rebuilding lists

What changes faces rather than moving them is an edit of a draft (`RoomDraft`): the room's
faces and vertices as plain data, each face with the `Vtx` bytes of its corners and the run
that should draw it. `planMeshRebuild` turns the drafts into writes:

- A list is cut into **runs**: maximal sequences of `gsSPVertex` and triangle commands.
  Every other command — the material, geometry mode, a call — is kept as it is.
- A run is rebuilt only when the faces drafted into it differ from the faces it drew.
  Untouched runs, and lists none of whose runs changed, are written byte for byte.
- A rebuilt run keeps its faces in their order, and loads vertices afresh in batches of at
  most 32 (F3DEX2's buffer), each into a `Vtx` array of the run's own; freed arrays are
  reused by name, else named `<list>_vtx_<n>`.
- A later run that draws vertices an earlier one loaded is rebuilt with it.
- Pointers into a list past a run that changed length are moved; a pointer into the middle
  of a rebuilt run is an error.
- Lit normals of the vertices a change touched are made again; cullable bounds are made
  again.

Every rebuild is run on the RSP and read back before it is made: the faces the new lists
draw must be the faces drafted, by position. A rebuild that does not read back is refused
with its reason, and nothing is written. Every vanilla room rebuilds without its first face
(`MeshCheckoutTest`), and every vanilla scene saves and reads back with a face taken out of
every room (`SceneSaveCheckoutTest`).

### Operations

`operateMesh` makes drafts of what is picked (`MeshOperations`, `MeshShapes`):

| Operation | What it does |
|---|---|
| Delete | Takes out every face that holds anything picked. |
| Flip | Swaps two corners of each picked face; its lit normals turn with it. |
| Merge at Center | One vertex at the middle of the picked ones; faces it flattens go. |
| Split | Picked faces get vertices of their own where they meet the rest. |
| Extrude | The region gets vertices of its own, and sides at its open edges; then moved. |
| Inset | Each face shrunk toward its middle and ringed; then scaled. |
| Subdivide | Edges cut in two, faces in four, neighbours cut where they meet a cut edge. |
| Add Box / Plane | Faces at the camera's target, drawn as a picked face is. |
| Move to Room | Faces taken out of their rooms and drawn in another's run of their material. |
| Assign Material | Faces moved into the run that draws the material in their room. |

A new corner's texture coordinates and colour are projected from the face it grew from
(`makeProjectedVtx`); a shape's are laid on each face's plane, a texel to a unit.

## Materials

What a face is drawn under is read while the mesh is: `MaterialTracker` follows the RDP
commands the frame ran — combiner, render mode, colours, tiles — and what was loaded into
texture memory, by the content of the bytes it came from, so two rooms' copies of one
texture are one. `listSceneMaterials` groups faces by that state and their name across
rooms: fast64's `mat_<room>_dl_<name>_layer<Pass>` list gives `<name>`, else the texture.

Renaming a material renames its list in every room and every pointer at it; duplicating
copies the list in every room, drawn by no face yet. A material is not yet copied into a
room that does not draw it, so assigning it there, or moving faces of it there, is an error
naming the room. Editing a material's combiner and textures is the material editor's,
which is a later plan.

## Layers

Room geometry is the exception to the rule that an edit of one layer never shows in
another: every layer that loads a room draws the same mesh, so an edit of the mesh changes
it for all of them, as an edit of collision does ([collision.md](collision.md#editing)).
Geometry in the scene's own segment shared by several rooms is edited in place for all of
them; the Properties panel warns of it where the picked vertices are.

## Saving

The save plan lists, above the files, every display list, `Vtx` array, bound and collision
symbol the save writes other than it was read (`listGeometryChanges`): how it changed, its
commands before and after, and how many bytes it grew by.
