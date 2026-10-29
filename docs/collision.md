# Collision

How a scene's static collision is read out of its bytes, drawn in the viewport, picked,
shown and edited. A save writes it as fast64 writes it ([scene-writing.md](scene-writing.md)).


In scope is the scene's own collision, the `CollisionHeader` that `SCENE_CMD_COL_HEADER`
points at: its polygons, surface types, cameras as their setting, and waterboxes. Out of
scope for now: an actor's collision (`DynaPoly`), where a camera stands and looks, following
an exit to its scene, and editing vertices and polygons' shapes, which waits for geometry
editing.

## Reading it

`readCollision` reads the header out of the scene's image the way `BgCheck_Allocate` takes it,
not out of the C, for the reason every header is read from bytes ([scenes.md](scenes.md)):
the bytes are what the game sees. The layouts of `CollisionHeader`, `CollisionPoly`,
`SurfaceType`, `BgCamInfo` and `WaterBox` are written down in Toki, as `SceneCmd` and
`ActorEntry` are; they are the game's, and have not changed since it shipped.

Each layer's header is read on its own (`SceneLayer::collision`), and a header several layers
point at is read once (`Scene::collisions`).

The header counts its vertices, polygons and waterboxes, but not its surface types or
cameras: the game only indexes them. Each is as long as the array the image laid out at its
address, as the alternate header list is. Where no symbol starts there, only as many entries
as the polygons and waterboxes use are read, and the reader says so.

What is wrong with a collision is a diagnostic naming the polygons, never a failed read, and
the polygon is kept as it is:

- a polygon naming a vertex past the vertex list;
- a polygon naming a surface type past the surface type list;
- a polygon whose stored normal is not the one its corners make, wound as the game winds
  them (within a cosine of 0.98).

Every vanilla scene's collision reads with none of these: 87,033 polygons
(`CollisionCheckoutTest`).

### What the words hold

A polygon's first two vertex words hold flags in their top three bits
(`COLPOLY_VTX_FLAGS_MASKED`) and the vertex's index in the rest (`getVertexIndex`,
`getVertexFlags`): the first's flags say what passes through it (`COLPOLY_IGNORE_CAMERA`,
`_ENTITY`, `_PROJECTILES`), the second's whether it is a conveyor floor.

A polygon is a floor, a wall or a ceiling as the game sorts it into its lookup lists
(`findPolygonKind`): a floor's normal is more than 0.5 up, a ceiling's more than 0.8 down,
and the rest are walls.

A surface type's two words are read field by field (`readSurfaceFields`), each at the shift
and mask of the `SurfaceType_Get*` function of the same name in `z_bgcheck.c`, which is also
what `SURFACETYPE0` and `SURFACETYPE1` pack. The words are kept as they are, so editing a
field later is writing it back into them. A waterbox's properties are read the same way, as
`WATERBOX_PROPERTIES` packs them: its camera, light setting and room, where room `0x3F` means
every room.

## Drawing it

Collision is drawn by the renderer like everything else, not over it: `makeCollisionDrawing`
builds F3DEX2 display lists into segment 15, as the actor markers are, so collision is depth
tested against the rooms, shows in the display list viewer, and is picked by what it drew.
One list sets up how polygons are drawn (shade colour only, no lighting, fog or culling, the
depth buffer on), then each polygon is a list of its own, three vertices and a triangle, so
each is a draw of its own the viewport can pick. Each polygon's colour is written into its
vertices, shaded by how its normal faces a fixed light, so that faces apart show apart.

Whether collision and waterboxes are drawn is the eye on their groups in the Scene panel,
each on its own, as paths are; collision starts hidden and waterboxes shown, since the
common view while editing is the rooms with their water. What the rooms are is the rooms'
own eyes, never the collision's: while any room is shown, collision is drawn after
everything else, see-through and as a decal (`G_RM_ZB_XLU_DECAL`), so it shows on the room
surface it lies on and is not written to the depth buffer; with every room hidden, it is
drawn opaque in their place, after the opaque pass, so actors still stand on it.

What collision is coloured by is the viewport bar's Collision Color, in its Overlays popover
while collision is shown ([viewport.md](viewport.md#the-bar)): Kind (floors, walls and
ceilings apart, the default), Surface Type (a colour of its own for each), or one setting:
Material, Floor Type, Wall Type, Exit, Hookshotable or Conveyor, where a polygon without the
setting is grey. Colours of adjacent values are kept apart by stepping the hue by the golden
ratio.

Every polygon has its edges drawn as lines in the overlay pass, so its shape reads at any
colour. Edges are drawn per draw by its faces (`makeFaceEdges`): an edge two triangles of one
draw share in one plane is inside a face and left out, so a quad shows no diagonal.

A waterbox is a see-through blue box from its surface down to the collision's lowest point,
its faces edged the same way, drawn after everything else (`G_RM_ZB_XLU_SURF`). Neither
waterboxes nor polygons hide with their room: hiding every room is how collision is seen
alone, and the water belongs in that view.

A change of Color By, or of the tinted surface type, builds the collision's image again; the
scene's own images are shared and untouched.

## Picking and Properties

Each polygon's draw is owned by its node (`CollisionPolygonRef`) and each waterbox's by its
own (`WaterBoxRef`), so a click selects the one under the mouse. While collision is shown, the
rooms' draws are owned by nothing: the collision lies on the room's surfaces, and a click on
it would otherwise pick the room drawn first. Rooms stay selectable in the Scene panel.
Waterboxes leave the rooms their owners, and so a click through water picks the waterbox,
the nearer of the two. A polygon or waterbox selected has no selection box around it
(`ViewportDraw::isBoxed`): its highlight already shows all of it.

A polygon selected shows in the Properties panel (`CollisionForm`): its kind, its vertices,
what it lets through, its normal and distance, and its surface type: each setting decoded
and named, the camera with its setting, and the exit with the entrance it leads to. Every polygon of the same surface type is tinted
(`findTintedSurfaceType`). A waterbox selected shows its bounds, surface, room, light
setting, camera and flag (`WaterBoxForm`). Each of these is edited in place; see below.

## The Scene panel

The layer's tree has a Collision group (`SceneOutliner`): its vertex count and bounds, its
collision materials, and its cameras by setting. A **collision material** is a surface type
and the flags of what passes through a polygon (`CollisionMaterial`), which is what fast64
sets on a material and what a face of the mesh is made of
([geometry.md](geometry.md#one-surface)). Each is named by both (`makeCollisionMaterialName`:
a summary of what its surface type sets, then "· Ignored by Camera" and the like), beside a
square of the colour Surface Type colours its polygons, with how many polygons it has, a
click selecting all of them, and that of the polygon selected highlighted and scrolled to.
A surface type no polygon has is listed alone. Waterboxes are a
group of their own beside it, a click selecting one.

## Editing

Collision is edited the way placements are ([scene-writing.md](scene-writing.md)): an edit
writes the bytes of the symbol it changes (`CollisionEdits`), the model lays the images out
again, and the collision is read again out of them, so what is shown is what was written.
Each edit is one undo step.

- **What passes through a polygon** and whether it is a conveyor are its vertex words' flags,
  set for every polygon selected at once. A flag some of them have and some not shows mixed.
- **Which surface type a polygon has** is picked from the collision's list, for every polygon
  selected. New gives them a copy of the one they have, so its settings can change without
  changing any other polygon.
- **A surface type's settings** are written field by field into its two words at the masks
  the game reads them with (`makeSurfaceType`); bits no field covers are kept. An edit
  changes the polygons of the collision material edited alone: a surface type it shares
  with polygons of other flags is copied for it first (`setMaterialSurface`), and material
  bindings to it follow. The Collision Materials group's menu adds a surface type and
  removes those no polygon has, renumbering the polygons after them; the last is kept.
- **Where polygons stand** is edited through the mesh they lie on, or as faces of their own
  where none does ([geometry.md](geometry.md#one-surface)).
- **A camera's setting** changes every surface type and waterbox naming that camera.
- **A setting with no effect** is shown with a warning sign at the right of its row, its
  tooltip saying why in the form "Ignored. Requires an exit." (`listIgnoredSettings`,
  `findIgnoredFlag`). What has no effect is read from the game's code: exits, floor types,
  floor properties, floor effects and conveyor speeds are read only from a floor; floor
  types 10 and 11 and floor effect 2 only change how an exit is taken; a conveyor's
  direction is read only with a speed, and a polygon's conveyor flag only with its surface
  type's speed; Contact Damage (bit 27) is read only on a wall or a floor of type 2 or 3.
  Bits 18 to 20 are always marked: only `func_80041D70` reads them, nothing calls it, and
  every vanilla surface type leaves them 0. Every field stays shown, since the game still
  reads what it holds.
- **A waterbox** is edited by its numbers, or in the viewport: moved by the transform tools
  as placements are, by the middle of its surface, never turned; resized by the handles on
  its four sides, which drag one side alone, previewed while held and one edit once let go.
  The Waterboxes group's menu adds one at the camera's target; a waterbox's menu duplicates
  or deletes it. A collision without waterboxes is given a list for the first, named as
  fast64 names it; deleting the last removes the list.

Collision is the exception to an edit of one layer never showing in another. The layers of
a vanilla scene almost always share one collision, and a change to the ground is meant for
all of them, so an edit changes the collision the layer reads, wherever else it is read.
The form says how many layers share it, and Make Own Copy gives the layer a copy of the
header and every list it points at (`makeOwnCollision`), named after the layer
(`<scene>_header02_collisionHeader`), after which its edits are its own.

Every vanilla scene's collision, edited (a polygon given a new surface type and a flag, a
waterbox added, the unused surface types removed, the second layer given its own copy), saves
and reads back as edited (`SceneSaveCheckoutTest`).

## Names

The decomp names most surface settings' values only by number (`FLOOR_TYPE_5`), so what they
are called comes from Toki's own `assets/collision.toml` (`readCollisionNames`), not from the
checkout: the value, the constant the decomp gives it, a name, and what the game does with it,
read from the game's code (`z_player.c` mostly). A value is shown by its name, its constant
faint beside it and what it does as its tooltip; one without an entry is shown as its number.
Only what the code was read for is named; the rest wait for someone to read it.

Camera settings are named by the checkout, from `CameraSettingType` (`readHeaderNames`), since
HackerOoT adds to them.
