# Concepts

The detailed write-ups behind [CLAUDE.md](../CLAUDE.md). CLAUDE.md holds the rules and the
architecture; each document here explains one part of the system in full: what it does,
why it is shaped that way, and what was measured.

| Document | About |
|---|---|
| [c-parsing.md](c-parsing.md) | How `cparser` reads decomp C without a build: the lazy preprocessor, layered macro tables, include resolution, embedded files. |
| [build-configuration.md](build-configuration.md) | Why `#if` must be answered as the build answers it, and how the Makefile and `config.h` become a macro layer. |
| [gbi.md](gbi.md) | Where gbi constants and the words of `gs*` macros come from. |
| [workspace.md](workspace.md) | How one checkout is read, kept current off the UI thread, and watched for changes. |
| [renderer.md](renderer.md) | Why the renderer is our own, what it takes from Fast3D, RT64 and parallel-rdp, and how its stages divide the work. |

A new concept gets its own document here and a line in this table, and a link from the
"Concepts" section of CLAUDE.md.
