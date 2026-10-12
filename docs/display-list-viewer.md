# The display list viewer

The Display List panel (View menu) shows the commands the viewport's last frame ran, in the
order the RSP ran them. It is written for reading and learning how a frame is drawn: what each
command is, what each of its fields means, and what it did in this frame.

## Where the commands come from

Nothing is read out of the C. The viewport runs its frame as a task with
`RspTask::recordsTrace` set ([renderer.md](renderer.md)), and the RSP records every command it
runs: its physical address, its two words, how deep in calls it ran, the outputs it produced
and how many triangles it culled. What is shown is therefore what the hardware model ran,
calls followed, branches taken and culled lists ended, not what a source file says.

Tracing costs memory, so the viewport traces only while the panel is open
(`ViewportPanel::setTracing`) and keeps the last traced frame with its content and a
generation number. The panel copies the frame it shows. Following the viewport, it takes a new
one at most twice a second; with *Follow the viewport* off, it holds the frame it has.

## Reading a frame: `FrameInspection`

`app/displayList/domain/FrameInspection` turns a traced frame into rows. It has no UI and is
tested like a parser.

- **Rows.** One row a command, decoded by `n64::decodeCommand` into its `gbi.h` macro and named
  fields ([gbi.md](gbi.md), "Reading words back"). A `gsSPDisplayList` row knows where its
  callee's commands end: the first later row at its own depth or above.
- **Names.** Addresses are named by the symbols of the frame's memory images, at the physical
  address each image was mapped to, as *symbol + offset*. Toki's own frame list is named too.
  A segmented address is resolved against the segment table as it stood when the row ran,
  tracked through every `gsSPSegment`.
- **Texture loads.** A `gsDPSetTextureImage` and the syncs, tile settings and loads after it at
  the same depth are one group, labelled with what it loads ("Loads a 32×32 RGBA16 texture",
  "Loads a palette of 16 colours"), and shown folded.
- **What it did.** `describeEffect` is a sentence for the commands whose effect depends on the
  frame: how many vertices went into which slots from where, how many triangles were drawn and
  culled, which list a call ran and for how many commands, where a segment was pointed, how a
  matrix was applied, what geometry mode bits changed, whether a cull list ended its list.
- **Draws.** Each RSP output that reaches the RDP as a primitive is paired with its RDP draw,
  so a row knows the triangles and rectangles it drew (`listDraws`), the RDP state they were
  drawn under (`findDrawState`) and where they landed on the screen (`listScreenTriangles`,
  through the frame's viewports; a triangle with a corner behind the eye is left out).

## Three levels of explanation

1. **A command.** Its row's tooltip says what the command does in general
   (`DecodedCommand::description`), and what it did here.
2. **A field.** The details pane lists the fields with their values named by the checkout's own
   `gbi.h` constants (`z64::GbiNames`, read off the UI thread). Each field's tooltip says what
   it means; geometry mode bits and combiner inputs are explained one by one.
3. **The state it drew with.** For a command that drew, the RDP state is written out
   (`describeRdpState`): each combiner cycle as `(a − b) × c + d` and what it reduces to, the
   blender as two weighted colours, the other mode's settings, the colours, and the tile
   descriptors. The tiles the primitive read are decoded out of the frame's texture memory
   with `fetchTexel` (`makeTilePreview`) and shown as images: what TMEM held, not the PNG.

The terms these lean on (texture memory, tile descriptor, cycle, combiner, blender, other mode,
geometry mode, segment, vertex buffer) have glossary entries (`findGlossaryEntry`), shown as
tooltips on the section headers.

## In the viewport

Selecting a row picks out what it drew in the viewport: for a call, everything the call ran.
The panel hands the screen triangles to `ViewportPanel::setHighlight`, in pixels of the frame
they came from, and the viewport scales them to its image.

## Jumping in

*Inspect in Display List* on an object's display list (Object panel) or a room's mesh (the
room's context menu in the Scene panel) opens the panel on the first command of that list in
the next traced frame, unfolding the calls around it. A list the frame does not run says so.
The lookup is by symbol name (`findSymbolRun`), so it holds across frames and images.

Placed actors' parts are not jump-in targets yet: their lists have names only in their object's
image, and the part rows in the Scene panel do not carry them.
