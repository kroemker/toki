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
- the collision vertices welded to the moved vertices (below), and the polygons they make
  faced and bounded again ([collision.md](collision.md)).

## One surface

The mesh and the collision are edited as one surface, because a new map's collision is
mostly its mesh, and an edit made twice is an edit forgotten once. Vanilla scenes bear it
out: about three in four of their polygons stand exactly on a face.

- **Pairing.** `pairSurface` pairs each polygon with the face at its three corners, the
  corners rounded to whole units as collision stores them, one face to a polygon, the first
  unpaired face winning. A paired face is **solid**, and its collision material
  (`MeshFace::collision`) is that of its polygon: a surface type and the flags of what passes
  through it ([collision.md](collision.md#the-scene-panel)). A polygon no face has is
  **solid only**, as an invisible wall is. A face whose corners in whole units are in a line
  can have no polygon (`canFaceBeSolid`), and is never solid: making it so is skipped, by
  Properties and by bindings alike, and a rebuild that leaves a polygon out for it is a
  warning.
- **Welds.** A collision vertex standing where mesh vertices stand is welded to them, and
  follows them when they move; when only some of the faces around it move, the polygons of
  the others keep it and those that moved are given a vertex of their own
  (`makeWeldedCollision`). A collision vertex moved moves the mesh vertices welded to it
  (`findWeldedMeshMoves`). Welds are always on.
- **Tools.** Every operation carries a face's collision material through what it makes of
  it: a face grown from a solid one is solid, a face taken out takes its polygon with it.
  After each rebuild the collision is made again from the drafts (`makeDraftedCollision`):
  polygons standing where they were read are kept as read, every other one is faced at its
  corners, vertices no polygon has are dropped, and the lists are written whole with their
  counts and bounds (`setCollisionGeometry`).
- **Picking.** Faces, edges and vertices are picked of one surface: every face of the shown
  rooms, solid or not, and, while the collision is shown, the solid faces of hidden rooms
  too and the solid-only polygons as faces of their own, edited with the same tools: what
  is drawn can be picked. Whether the collision is shown is a switch of the viewport's
  Overlays. A click on a material under Materials in the Scene panel, render or collision,
  selects by faces and picks its faces. The surface is read whenever the scene or its layer is, whatever
  the viewport selects by. Whether a face is solid is a property of it,
  shown and set in Properties, never a mode of the viewport.
- **Properties.** The picked faces show whether they are **Drawn** and **Solid**, and their
  **Collision Material**. A face made solid is made of what its material's solid faces are
  made of, else what most polygons are (`findDefaultCollision`). A solid face no longer
  drawn keeps its polygon as solid only; a face neither drawn nor solid is gone. Their
  polygons are edited below as polygons are.
- **Bindings.** A render material can be bound to a collision material, or to not being
  solid, in its menu under Materials. Its faces are made so at once, as are faces it is
  assigned to later, and their collision is then not set in Properties: it is driven by the
  material. Binding a material whose faces differ in their collision asks first, naming how
  many change. A binding names its surface type by its two words, not its index, so it
  means the same in every collision of the scene, and it follows a surface type whose
  settings are edited. Bindings are saved with the scene in `.toki/scenes/<scene>.toml`,
  keyed by the material's name where every room sets it by a list of its own, else by its
  RDP state; renaming the material carries its binding. They change how Toki edits, never what the game reads: each read checks them,
  and faces not as their material is bound are a warning, the material's menu offering to
  re-apply or unbind the binding.

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
it for all of them, as an edit of collision does ([collision.md](collision.md#editing)). A
layer pairs its mesh with the collision it reads, its own copy where it has one.
Geometry in the scene's own segment shared by several rooms is edited in place for all of
them; the Properties panel warns of it where the picked vertices are.

## Saving

The save plan lists, above the files, every display list, `Vtx` array, bound and collision
symbol the save writes other than it was read (`listGeometryChanges`): how it changed, its
commands before and after, and how many bytes it grew by.
