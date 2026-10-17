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
looks rather than what it is: a scene's light setting, and whether its actors are hidden,
marked, or drawn as their models. The content's owner offers them as `ViewportChoices` with a
handler, the bar shows a row for each choice it was offered, and the owner applies what is
picked to its own state and gives the viewport new content. The viewport never knows what a
scene is; an owner that offers nothing gets no rows.

## The bar

Above the image, from the left: a button for each shading (Alt+1 to Alt+5), the Shading
popover, and at the right a switch that hides
every overlay at once (Shift+Alt+Z) and the Overlays popover, whose button counts the overlays
that are on. Every popover is a form of two-column rows. Hotkeys act only while the cursor is
over the viewport.

A chip in the image's top right corner names everything the view draws differently from
what the game would (`listViewChanges`), so that a debug view is never taken for the game.

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
  it was, so a cutout keeps its shape in every shading.
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
