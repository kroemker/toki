# The viewport

How the viewport is told what to draw, how it chooses how to draw it, and what it reports
about each frame. How a frame is run is in [renderer.md](renderer.md); what a scene puts in it
is in [scenes.md](scenes.md).

## Content and view

What the viewport draws is a `ViewportContent`: memory images and the display lists in them
to run, set by whichever panel owns the viewport (the Scene panel, the Object panel). How it
draws them is a `ViewportView`, which belongs to the viewport itself and is the same whatever
it draws. The view is chosen in the bar above the image and kept in `config.toml` under
`[viewport]`.

Some choices belong to the content but are made in the bar, because they change how it
looks rather than what it is: a scene's light setting, whether its actors are hidden,
marked, or drawn as their models, and what colours its collision
([collision.md](collision.md)). Whether the collision and waterboxes are shown is the
Scene panel's, in its tree; the choices only carry it, for the bar to count and name. The content's owner offers them as `ViewportChoices` with a
handler, the bar shows a row for each choice it was offered, and the owner applies what is
picked to its own state and gives the viewport new content. The viewport never knows what a
scene is; an owner that offers nothing gets no rows.

## Picking and moving

The same way, the owner gives the viewport points to pick (`setPickables`) and what is
selected to transform (`setTransformTarget`), each with what to do about it. A click, a press and release that moved less than a few points, picks the point
drawn nearest the mouse, if one is within 12 points of it (`findPickedPoint`); a left drag
draws a box and picks every point in it (`findPointsInRect`). A click that hits nothing picks
nothing, which clears the selection.

A click that is near no point picks what drew the triangle under the mouse: the RSP marks
where each command of the frame's own list began its outputs (`n64::ListMark`), so every
draw knows which outputs it made (`Frame::drawOutputs`), and a draw names its `owner`, a
number the content's owner gives it. What is selected is passed back as owners
(`setSelectedOwners`) and shown two ways, each an overlay of the bar that is kept with the
others: what its draws drew, tinted and outlined in the overlay pass (Selection Highlight);
and a box around that, in the world, taken back from clip space by the inverse of the
frame's view and projection (Selection Box), so it covers what is on screen.

The mouse is split as Blender splits it: the left button picks, the middle or right button
orbits, and either with Shift pans; the wheel zooms.

How a pick changes the selection (`PickMode`) is a group of three icon buttons in the bar:
pick anew, add, or take away. Holding Shift lights Add and holding Ctrl lights Take Away for
as long as it is held, so the bar always shows what the next pick will do. In the Scene
panel's tree, Shift+click adds and Ctrl+click toggles, as lists do.

The Scene panel keeps the selection as a `SceneSelection` of nodes, rooms or placements, the
one picked last the active one the Properties panel shows. A transform of several moves or
turns them all together, as one step to undo.

## Transforms

Every way of moving or turning what is selected runs one `TransformSession`: the Move and
Rotate tools' gizmo (ImGuizmo), and G and R, which move or turn it with the mouse alone, as
in Blender. A session knows only points, an offset and an angle, so the same one will move
vertices. While it runs a line over the image says what it does, `Move  X 120  Y 0  Z -30`; nothing changes the scene until it is
confirmed, with Enter or a click, and Esc or a right-click cancels it. Typing a number takes
the mouse's place, field by field, Tab moving to the next axis, Backspace and `-` as
expected. Numbers are read from the keys pressed, digits, `-` and `.` or `,`, not from text
input, which SDL sends only while a text field has focus. ImGui names punctuation keys by
their place on a US keyboard, so for those SDL is asked what the key prints under the
layout in use (`SDL_GetKeyFromScancode`): `-` is the minus key on a German keyboard as on a
US one.

G and R begin with no axis held, whatever the bar holds, and once they end the bar holds
what it held before, so an axis held for one move is not held for the next turn. Switching
tools lets go of the axes too.

What a session comes to is previewed as it runs. Whenever its result changes, the target's
owner is told (`ViewportTransform::PreviewHandler`) and shows it as `setNextFrame` content
without editing anything: the scene panel draws a copy of the shown layer with the moved
placements in it (`makeSceneContent` with a layer in place of the scene's), so a model moves
and turns with everything it draws, its skeleton included. When the session ends the handler
is told so, and the scene is shown as it is again, edited if it was confirmed. The preview
re-runs the frame, which costs what orbiting the camera costs.

The options of the Move and Rotate tools are in the bar, as buttons: the axes held, X, Y and
Z, and snapping. X holds to X alone or frees it again, Shift+X holds to the other two, and
the buttons light as the keys press them; the gizmo shows only the axes held, and each axis
held is drawn as an endless line through the middle of what is selected, red, green and
blue, as in Blender, cut where it passes the camera (`findScreenLine`). The grid
snaps a drag, not what is typed, to 10 units or 15°. A click on a button switches it; while
transforming, holding Ctrl switches the grid and Alt the ground for as long as it is held,
as in Blender, and the button shows what holds now. The bar's buttons work while G or R
runs: only a click on the image itself confirms or cancels. The ground snap sets each moved point
down on the highest surface within 100 units above it and below: a ray straight down against
the room triangles the frame drew, taken back to the world by the inverse of the frame's
view and projection, so only what is on screen is ground until collision is read.

A turn is about one axis, Y unless another is held. A turn about Y adds to an actor's Y
angle exactly, since the game applies Y first; a turn about X or Z is applied to the actor's
whole Y·X·Z rotation and read back into three angles (`rotateBinaryAngles`), which changes
every angle, so it is offered only when no selected actor reads params from its rotation,
and never for a transition actor, which has Y alone. A blocked axis says why on its button.
Scale is not built, and says that actors set their own scale.

What a click selects and what dragging does are the viewport's own (`ViewportTools`). The
selection mode is a group of icon buttons at the start of the bar: Node, Face, Edge and
Vertex, keys 1 to 4. Node mode selects what the scene's tree holds, rooms and placements,
and is not called Object, because an object is what the decomp loads data from. The tool is
a vertical group floating over the image's top left: Select, Move, Rotate and Scale, keys Q,
W and E, Scale unbound. Only Node, Select, Move and Rotate are built; the rest are shown
disabled, saying so. Select is the tool the viewport starts with and draws no gizmo.

What is selected may also offer handles (`setHandles`, `ViewportHandles`): points that drag
along one direction alone, such as a waterbox's sides, drawn in Node mode with any tool. A
handle under the mouse takes the press from the viewport's button, as the gizmo does. A drag
is measured along its direction as that looks on the image (`findAxisDistance`), in whole
units; it is shown through its preview handler while held and handed to its owner once let
go, one edit however far it went, and Escape or a right click cancels it.

Every key is bound through `core::KeyBindings`: each action has a name, a scope (the whole
window, or the viewport while the cursor is over it) and a default chord, and `config.toml`
binds another under `[keys]`, `move_tool = "G"`, writing only those changed from their
default. Menus and tooltips name the key from the same bindings (`makeShortcutText`), so
what they say is what is bound, and nothing is reached only by a key.

## The bar

Above the image, from the left: a button for each shading (Alt+1 to Alt+6), the Shading
popover, and at the right the X-ray switch (Alt+Z), a switch
that hides every overlay at once (Shift+Alt+Z), and the Overlays popover, whose button
counts the overlays that are on. The content's choices are rows of the Overlays popover:
the scene's Actors, and, while its collision is shown, Collision Color. Every popover is a
form of two-column rows. Hotkeys act
only while the cursor is over the viewport.

## Overlays

Everything drawn over the frame is drawn in the overlay pass ([renderer.md](renderer.md)),
tested against the frame's depth, never in ImGui's draw list over the image:

- **Flag Links**, the lines the content's owner gives as a `ViewportOverlay`.
- **Highlight**, what the display list viewer's selected command drew, filled and outlined
  in the accent colour. It is drawn whether or not the overlays are shown, always faintly
  where the frame hides it, and only over the frame it was found in.
- **Grid**, a floor grid on the XZ plane around the camera's target, a line every 100 units,
  the X axis red and the Z axis blue.
- **X-Ray** draws what the frame hides of every overlay faintly.

Changing an overlay draws the last frame again without running it: the RSP's and RDP's
results are kept (`DrawnFrame`), so hovering a flag costs a draw, not the whole frame.

A chip in the image's top right corner names everything the view draws differently from
what the game would (`listViewChanges`), so that a debug view is never taken for the game.
Collision and waterboxes the content's choices show are named there too.

A draw can ask for its edges (`ViewportDraw::isEdged`), as collision does: the edges of the
faces its triangles make are drawn as lines in the overlay pass (`makeFaceEdges`), an edge
two of its triangles share in one plane left out.

## Render modes

A `RenderMode` is how a frame is drawn: its shading, five stages of the pipeline that can be
turned off, and the texture filter. The default is the frame the game would draw. Every other
choice is made on the CPU, by changing what the RSP is asked to do or the RDP state each
primitive is drawn under, so the ubershader never learns of it and the GPU tests that pin it to
`n64/` still hold.

- **Shading.** N64 is the frame as the RDP draws it. Shade Only, Texture Only, Normals and
  Depth each give every triangle a copy of its state whose colour combiner selects one input
  (`applyRenderMode`): its shade; its first texture, or its shade where it has none; or a
  shade written over its vertices, the normal of its face in world space, or its depth between
  the nearest and farthest vertex of the frame, near white and far black. Alpha is combined as
  it was, so a cutout keeps its shape in every shading. Wireframe draws no surface at all,
  only the edges of every triangle the frame drew, in the overlay pass
  ([renderer.md](renderer.md)), none hidden, an edge two triangles share once. Edges over
  shaded faces are left to an edge select mode when editing comes.
- **Lighting.** Off, the RSP shades lit vertices white (`n64::RspOverrides::lightsWhite`), so
  textures show without the light setting. Texture generation still reads the normals.
- **Fog.** Off, a blender cycle that reads the fog colour passes the pixel through.
- **Backface Culling.** Off, the RSP culls no triangle for the way it faces
  (`n64::RspOverrides::ignoresCulling`).
- **Alpha Cutout.** Off, the alpha compare and the coverage that reads alpha are cleared.
- **Translucency.** Off, a forced blend with memory passes the pixel through, opaque.
- **Texture Filter.** Point clears the other mode's filter, so each texel is a square.

None of these is something a game could not have asked for itself except the two RSP
overrides, which the hardware has no switch for and are kept apart from F3DEX2 in the task.

## Statistics

The line under the image reports the last frame the viewport rendered, and each part of it
opens a breakdown above it:

- **Geometry**: the triangles the RSP sent and culled, the vertices it loaded, the commands
  it ran (`n64::RspStatistics`, counted whether or not the frame was traced), the texture
  loads the RDP ran, and the GPU's draw calls after batching.
- **Memory**: how much of the 16 MiB the RSP addresses the frame filled, from address 0 to
  the end of its own display list: the game code's region below `FirstImageAddress`, every
  image by kind (scene, rooms, objects, actors, generated), the frame's display list, the
  images left out because they did not fit, and the largest images. Images are named by the
  content's owner (`ViewportContent::labels`); one without a name is called by its index.
- **Frame**: how long each step took on the CPU, with a history of the last 120 frames:
  placing the images and writing the frame's display list, the RSP, the RDP, and building,
  uploading and submitting the draw calls.

The viewport renders only when something it shows changed, so the cost of the last frame is
what is shown, not a frame rate of the scene; the application's own frame rate is shown
beside it. The GPU's time is not measured: SDL's GPU API has no timestamp queries.
