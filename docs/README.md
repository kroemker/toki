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
| [objects.md](objects.md) | What an object holds, how its assets are catalogued by type, and how a skeleton is posed and drawn the way an actor draws it. |
| [textures.md](textures.md) | Where a texture's format and palette come from, how it is decoded as the RDP reads it, and how the Textures panel shows it. |
| [actor-rendering.md](actor-rendering.md) | Drawing placed actors as their objects, by reading the literal calls in their overlay's `Init` and `Draw`; what is built and what is planned. |
| [workspace.md](workspace.md) | How one checkout is read, kept current off the UI thread, and watched for changes. |
| [renderer.md](renderer.md) | Why the renderer is our own, what it takes from Fast3D, RT64 and parallel-rdp, and how its stages divide the work. |

A new concept gets its own document here and a line in this table, and a link from the
"Concepts" section of CLAUDE.md.
