# gbi constants and `gs*` words

Where the values of gbi constants and the display list words of `gs*` macros come from
when C is turned into a memory image. Not built yet; it belongs with the memory image
(package 8 in [WORK_PACKAGES.md](../WORK_PACKAGES.md)).

The renderer does not need any of this. It reads words, and its own decoder in `n64/`
knows the F3DEX2 opcodes as hardware facts. This document is only about writing words.

**The words come from the real macros.** `gsDPLoadTextureBlock` expands to seven commands,
not one, and a hand-written macro-to-words table covering `gbi.h` is both a lot of code
and a thing that goes stale.

**Which `gbi.h`** is still open, between two candidates:

- **The checkout's own `include/ultra64/gbi.h`**, preprocessed under the configuration's
  `GBI_DEFINES` and expanded by `cparser` at runtime. Nothing is vendored, HackerOoT's own
  `gs*` additions come for free, and the words follow the checkout exactly. It needs the
  `##` operator in `Expand` (`gsDPLoadTextureBlock` pastes `siz##_LOAD_BLOCK`), `&sym` in
  `Expression`, and `gbi.h` read as a configured unit rather than scraped, since most of it
  sits in `#ifdef F3DEX_GBI_2` arms.
- **A vendored N64 `gbi.h`** and a build-time generator that emits a dispatcher calling the
  real dynamic `g*(pkt, ...)` macros. No `##` needed, but the header is frozen, and
  HackerOoT's additions are not in it.

The first is the leaning: the decomp source tree is the only input, and it is what the
build itself compiles. libultraship's `gbi.h` is not a candidate: it encodes PC words.
