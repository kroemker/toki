# gbi constants and `gs*` words

Where the values of gbi constants and the display list words of `gs*` macros come from.
Not built yet; it belongs with the memory image (package 8 in
[WORK_PACKAGES.md](../WORK_PACKAGES.md)), and the Fast3D spike (package 5) decides which
`gbi.h` it reads.

**gbi constants come free.** The `gs*` macro set is a frozen 1996 SDK header. Vendored
Fast3D ships `gbi.h`, so `G_RM_AA_ZB_OPA_SURF`, `G_CC_MODULATEIA` and friends are real
values compiled into the binary. Resolve identifiers against a generated name-to-value
map; never parse decomp headers for them.

**The words come from the real macros.** `gsDPLoadTextureBlock` expands to seven commands,
not one, and a hand-written macro-to-words table covering gbi.h is both a lot of code and
a thing that goes stale. Instead a build-time generator scrapes `gbi.h` for macro names and
arity and emits a dispatcher that calls the actual dynamic `g*(pkt, …)` macro with the
evaluated arguments, letting the SDK header encode the words itself.
