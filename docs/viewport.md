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

Above the image, from the left: the Shading popover, and at the right a switch that hides
every overlay at once (Shift+Alt+Z) and the Overlays popover, whose button counts the overlays
that are on. Every popover is a form of two-column rows. Hotkeys act only while the cursor is
over the viewport.

A chip in the image's top right corner names everything the view draws differently from
what the game would (`listViewChanges`), so that a debug view is never taken for the game.

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
