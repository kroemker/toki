# Concepts

The detailed write-ups behind [CLAUDE.md](../CLAUDE.md). CLAUDE.md holds the rules and the
architecture; each document here explains one part of the system in full: what it does,
why it is shaped that way, and what was measured.

| Document | About |
|---|---|
| [c-parsing.md](c-parsing.md) | How `cparser` reads decomp C without a build: the lazy preprocessor, layered macro tables, include resolution, embedded files. |
| [build-configuration.md](build-configuration.md) | Why `#if` must be answered as the build answers it, and how the Makefile and `config.h` become a macro layer. |
| [gbi.md](gbi.md) | Where gbi constants and the words of `gs*` macros come from. |
| [memory-image.md](memory-image.md) | How asset C becomes per-segment buffers byte for byte as the build compiles it: preludes, type layouts, initializers, relocations, layout. |
| [scenes.md](scenes.md) | How a scene is read from its folder, its headers out of the memory image, and drawn the way `Play_Draw` draws it, and how a fast64 export reloads. |
| [collision.md](collision.md) | How a scene's collision is read out of its bytes, drawn by the renderer over or in place of the rooms, picked, and named from Toki's own `collision.toml`. |
| [scene-writing.md](scene-writing.md) | How a scene is saved: its C written again from the memory image in fast64's layout, typed writers over a generic one, and checked symbol by symbol before anything is written. |
| [geometry.md](geometry.md) | How the rooms' meshes are read, picked and edited: vertices written back into their `Vtx`, faces changed by rebuilding only the runs that draw them, checked on the RSP, and materials listed across rooms. |
| [objects.md](objects.md) | What an object holds, how its assets are catalogued by type, and how a skeleton is posed and drawn the way an actor draws it. |
| [textures.md](textures.md) | Where a texture's format and palette come from, how it is decoded as the RDP reads it, and how the Textures panel shows it. |
| [actor-rendering.md](actor-rendering.md) | Drawing placed actors as their objects, by reading the literal calls in their overlay's `Init` and `Draw`; what is built and what is planned. |
| [actor-database.md](actor-database.md) | The actor database that says how each actor is shown by its params, one per checkout in `.toki/`, seeded from Toki's own and derived from the C for the rest; what is built and its plan. |
| [messages.md](messages.md) | How messages are read, encoded byte for byte as the build does, laid out and drawn as the game draws a text box, edited as markup, and saved into a message file Toki owns. |
| [workspace.md](workspace.md) | How one checkout is read, kept current off the UI thread, and watched for changes. |
| [tools.md](tools.md) | How the tools change the checkout: planned and shown before they are written, never over a change they did not see, into blocks of their own; and how the configuration headers are read as settings. |
| [display-list-viewer.md](display-list-viewer.md) | How the Display List panel reads the viewport's traced frame into commands, names and sentences, the RDP state and texture memory behind each draw, and how it is opened from other panels. |
| [viewport.md](viewport.md) | How the viewport is given content and chooses how to draw it in its bar, and what it reports about each frame. |
| [renderer.md](renderer.md) | Why the renderer is our own, what it takes from Fast3D, RT64 and parallel-rdp, and how its stages divide the work. |

A new concept gets its own document here and a line in this table, and a link from the
"Concepts" section of CLAUDE.md.
