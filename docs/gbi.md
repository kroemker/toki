# gbi constants and `gs*` words

Where the values of gbi constants and the display list words of `gs*` macros come from
when C is turned into a memory image.

The renderer does not need any of this. It reads words, and its own decoder in `n64/`
knows the F3DEX2 opcodes as hardware facts. This document is only about writing words.

**The words come from the real macros.** `gsDPLoadTextureBlock` expands to seven commands,
not one, and a hand-written macro-to-words table covering `gbi.h` is both a lot of code
and a thing that goes stale.

**The `gbi.h` is the checkout's own** `include/ultra64/gbi.h`, the one the build compiles.
Nothing is vendored, HackerOoT's own `gs*` additions come for free, and the words follow
the checkout exactly. A vendored header with a generated dispatcher was the alternative;
it would freeze the header and miss HackerOoT's additions. libultraship's `gbi.h` was
never a candidate: it encodes PC words.

## Reading it

`hackeroot::readGbi` reads `gbi.h` the way `readConfiguration` reads `config.h`: against
the configuration alone, then layered over the workspace's macros. Against the configuration
alone, every `#ifdef F3DEX_GBI_2` is answered from the Makefile's `GBI_DEFINES`, and the
headers `gbi.h` includes (`mbi.h`, `ultratypes.h`) are read too instead of being skipped for
include guards the header scrape has seen. Layered over the workspace, the rest of the
checkout's macros stay visible to the arguments of a command.

A name the gbi headers define somewhere, but not under this configuration, is hidden in the
layer, `gbi.f3dex3.h`'s names included. Otherwise the scrape's first-seen definition would
answer for it, and a command the build would reject, such as an F3DEX3-only one in an
F3DEX2 build, would encode as something instead of being reported.

`gbi.h` wraps its whole C half in `#if defined(_LANGUAGE_C)`, which no Makefile spells: MIPS
GCC defines it on its own. The configuration layer therefore starts with the compiler's
predefines (`hackeroot::readConfiguration`).

A checkout that enables F3DEX3 is refused with a diagnostic saying so, rather than drawn
wrong.

## Turning a list into words

`z64::DisplayListEncoder` takes the interior of a `Gfx` array's braces. Each element is
expanded on its own, so every command keeps the source location of the `gs*` call it came
from, and evaluated as one or more `{ w0, w1 }` pairs in the decomp's dialect. Braces may be
left out, as C allows: the real `gsSPTextureRectangle` writes its first command as two bare
words, and loose words pair up the way C fills the array.

The macros an element expands against are the gbi's with the file's own layered over them
(`cparser::MacroTable::defineFrom`), because commands name sizes the file's header defines,
such as `spot00_room_0Tex_WIDTH`. What the encoding needs from `cparser`:

- `##` and `#` in `Expand`, because `gsDPLoadTextureBlock` pastes `siz##_LOAD_BLOCK`. The
  pasted text lives in a `Spellings` the caller owns.
- Casts spelled in several words, `(unsigned int)(s)` in every address word.
- `sizeof` of the gbi's aggregates (`Vtx`, `Mtx`, `Vp`, `Light`, `uSprite`), which
  `z64::getDecompDialect` sizes.
- Symbol addresses with C's pointer arithmetic. The caller answers `findSymbol` with an
  address and an element size, so `&sVtx[13]` and fast64's `sVtx + 13` both move by
  thirteen vertices, and a cast to an integer stops the scaling.

An element that no gbi macro covers is an error naming it and makes no commands. A symbol
nothing answers is a warning naming it, and reads as zero.

## Cost

The real gbi is verbose: one `gsSP2Triangles` expands to over 700 tokens. Two things keep
that affordable. `Expand` interns hide sets, so carrying one with every token never
allocates. And an encoder remembers the words of every element it encoded cleanly, keyed by
its spelling, since its macros and symbol addresses never change; display lists repeat
themselves. Every display list of the vanilla scenes and objects, 7,512 lists and 495,027
commands, encodes in a few seconds in a release build, without a diagnostic. A checkout test
holds both, and checks the words of the common commands against F3DEX2's known encodings.

Symbols that live in the game's code rather than in an asset, such as `gIdentityMtx`, are
the memory image's to answer.
