# Build configuration

Why an `#if` has to be answered the way the build answers it, and how the checkout's
Makefile and `include/config.h` become the macros every file is read against.

## Where `#if` matters

**Asset `.c` files need it.** fast64 emits `#if ENABLE_ANIMATED_MATERIALS` around a scene
command, and the answer lives in the checkout's own `include/config/*.h`.

**Ordinal table files need it most.** `SceneID` and `ActorID` come from position in an
X-macro table, so reading one conditionally-disabled entry shifts every subsequent ID by
one — silent, and it corrupts actor placement and scene registration. `readTable` therefore
preprocesses with `strictConditionals`, which raises an unresolved identifier in a
condition from a note to a warning. An `#if` in a table is never read quietly.

## Resolving a name is not the same as resolving it correctly

The scrape knows every flag, but only its first-seen arm, and the flags tables actually
hinge on (`DEBUG_FEATURES`, `OOT_VERSION`, `PLATFORM_*`) are not in any header at all:
they are `-D` arguments the Makefile builds. So conditions are answered against a
**configuration layer** between the scrape and every unit (`hackeroot/Configuration`):

- `readBuildDefines` reads the checkout's own Makefile with `makeparser`, a small GNU make
  evaluator — assignments of every flavour, conditionals, includes, `define`, the text
  functions — and takes `CPP_DEFINES` and `GBI_DEFINES` as make would expand them.
  Running make is not an option: parsing HackerOoT's Makefile alone runs `mkdir -p`,
  `find` over the tree and `cpp` over the spec. So nothing is executed; `$(shell)` expands
  to nothing and marks what it fed as uncertain, and only an uncertain *define* is
  reported. The one `$(shell)` whose output matters, `VERSION_MACRO`, is derived and
  passed as a command-line override, which is what make itself would do with it.
- `readConfiguration` turns those arguments into source text and preprocesses
  `include/config.h` under them, against an empty base (the scrape has seen `CONFIG_H`
  and would skip the file). The result becomes a layer over the scrape, and every name
  the config headers define in some arm but not in this configuration is hidden, so the
  scrape's first-seen arm can never answer for it.

The compiler's own predefines come first in that layer, because headers branch on them and
no Makefile spells them: `_LANGUAGE_C` (which `gbi.h` wraps its C half in), `__GNUC__`
(which `cutscene_commands.h` picks its float encoding by), `__STDC_VERSION__` and the MIPS
target macros, as `mips-linux-gnu-gcc` 10 defines them under the Makefile's flags. Without
them a cutscene encodes its floats as integers and the gbi defines nothing.

**Some files must be read the way the compiler reads them, not against the scrape.** The
scrape is a cheap stand-in for headers a unit does not follow, and it has seen everything,
`stdbool.h`'s `true` included. When the bytes must match the build, as the memory image's
must, a file's system headers are read in full under the configuration alone, as a
*prelude* (`hackeroot::readPrelude`), and expanded as they are read, the way a compiler's
preprocessor does, so X-macro enumerations come out right. See
[memory-image.md](memory-image.md).

The user's build settings (`VERSION`, `DEBUG_FEATURES`, ...) are make's command line, and
flow through exactly as they would in `make VERSION=...`. The same settings must reach the
build runner, or the viewer and the build disagree about every scene ID.

## Measured

Against a HackerOoT checkout over `\\wsl.localhost` (`VERSION ?= ntsc-1.2`,
`DEBUG_FEATURES ?= 1`): make evaluation 95 ms for its two passes, the configuration 38 ms.
The scene table reads as 103 rows with `oceantest_scene` at 102, and the scene, actor,
object and entrance tables read with no warnings. `tests/hackeroot/CheckoutTest.cpp`
checks this against a real checkout when the build is configured with
`-DTOKI_HACKEROOT=<path>`.
