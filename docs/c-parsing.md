# Reading C

How `cparser/` reads decomp C without a build, and why it is built the way it is.

## A real preprocessor, but a lazy one, over a targeted parser

The prototype expanded all of `gbi.h`/`z64.h` up front to learn what `gsSPVertex` meant;
that was the source of its multi-second load times. The fix is not to skip expansion —
real asset C cannot be read without it — but to expand only what a consumer asks for, and
to take macro *definitions* from a cheap scrape instead of from resolving the include
graph.

What the checkout actually contains decided this. fast64 emits `#if
ENABLE_ANIMATED_MATERIALS` inside a `SceneCmd[]`, writes rotations as
`DEG_TO_BINANG(0.000)`, and sizes arrays with a `#define LENGTH_…` from a sibling header.
Extracted vanilla assets are a different shape again:

```c
u64 spot00_scene_00013D98_Tex[TEX_LEN(u64, spot00_scene_00013D98_Tex_WIDTH, …, 16)] = {
#include "assets/scenes/overworld/spot00/spot00_scene_00013D98_Tex.rgba16.inc.c"
};
```

That include is spelled from the repository root, sits inside an initializer, and names a
file that does not exist — the build generates it from a `.png`.

## The pieces

1. **A lexer**, `Scanner`. Comments and line continuations are trivia; `#` comes back as
   an ordinary punctuator carrying a line-start flag. A token points at the `SourceFile`
   it came from and its text is a `string_view` into that file's contents, so whatever
   owns the file must outlive the token.
2. **`MacroTable`, filled by a define-only scrape** of the checkout's header tree
   (`Scrape`). One linear scan per header, conditionals ignored, first definition of a
   name wins. This is how `SCENE_CMD_ROOM_LIST`, `BLEND_RATE_AND_FOG_NEAR` and the
   `config/` feature flags become known without resolving a single include, and it tracks
   decomp changes on its own rather than going stale. Both arms of an `#if` are seen; that
   is the price, and it is cheaper than the alternative. The flags that price would get
   wrong are answered by the configuration layer instead; see
   [build-configuration.md](build-configuration.md).
3. **`Preprocessor`** — directives, real `#if`/`#elif`/`#ifdef` evaluation, `#define`, and
   include splicing into one token stream. Macro uses are *left standing*: which of them
   to expand is the consumer's decision, and `gs*` must never be expanded at all.
4. **`Expand`** — on-demand macro expansion of a token range, with a stop-list. Every
   token carries a hide set, as the standard describes, and a replacement is rescanned
   together with the tokens after it, so `#define F G` then `F(1)` reaches `G(1)`.
   Variadic macros, both `...` and GNU's `args...`, are supported. `#` and `##` are
   diagnosed, not implemented.
5. **`Value` and `Expression`** — a constant expression evaluator over the C operators the
   decomp uses, including casts that truncate the way the named type does. Integer and
   floating point both, because `DEG_TO_BINANG` is `(s16)(s32)((degrees) * (0x8000 /
   180.0f))`, and `float` stays `float`. Which type names a cast may use, and how wide
   they are, comes from the caller's `Dialect`. `ARRAY_COUNT` and `TEX_LEN` are builtins
   of the decomp's dialect (`z64::getDecompDialect`) rather than expansions: their real
   definitions are written with `sizeof`, which nothing here can answer.
6. **`Parser`** — file-scope declarations only, as `{name, type, arrayLength, initializer
   token span}`, one per declarator in a list. Typedefs, struct definitions, functions
   and function prototypes are stepped over; anything else it cannot read is a note. The
   initializer stays *unevaluated tokens*, because the memory image and the scene reader
   want different things out of it and neither should pay for the other's reading.
7. **`TableReader`** — X-macro tables read into ordinal rows, preprocessed so that an
   `#if` around an entry is answered rather than guessed.

## Ownership is layered

Nothing reads into shared mutable state. The scrape's `ScrapeResult` owns the header files
and the base `MacroTable`. Each `preprocess` call returns a `TranslationUnit` that owns its
own `SourceSet` and a `MacroTable` layered over that base: its `#define`s, `#undef`s and
include guards live in the layer and never reach the base, so reading a file twice reads
it the same way and one scene cannot leak into the next. A reload is a new unit; the old
one stays valid until dropped.

The only lifetime rule is that the base outlives, and does not move under, every unit
read against it. Layers stack: the build configuration is one more layer between the
scrape and every unit.

## Include resolution

Two roots, tried in order: the including file's own directory, then each configured search
path — for HackerOoT the repository root and `extracted/<version>`, which is what the
Makefile passes to `cpp`. An include is followed only when it resolves *inside* an allowed
root, so a scene reaches its rooms and never descends into `z64.h`. A missing include is a
diagnostic naming it, never a failure. Cycles are broken on the active include stack, not
on a visited set: the same `.inc.c` is legitimately included by two different arrays.

**A `.inc.c` that does not exist is a texture.** `X.rgba16.inc.c` resolves to the
`X.rgba16.png` the build would have generated it from, as an `EmbeddedFile` tagged with
the format its name spells. It also stands in the token stream as an `Embedded` token
where the include was, so the declaration it fills holds it in its initializer. A
paletted texture's image also carries the palette it was extracted against, so the
include's spelling is only a prefix of the filename. Never parse a vanilla texture as C:
the PNG is both faster and the only thing actually on disk.

The rule is `z64::resolveTextureInclude`, passed to the preprocessor as its
`resolveMissingInclude`; `cparser` itself knows only that some includes embed a file.

**Every filesystem question is answered from a cached directory listing.** A checkout on
`\\wsl.localhost` charges milliseconds per `stat` and a vanilla scene asks hundreds:
answering them one at a time cost 3 s per scene, a listing per directory (`DirectoryCache`)
costs 0.6 s cold and 0.09 s warm. Nothing in the parsing layer may call `exists` in a
loop.

## Unknown macros

HackerOoT adds its own `gs*` macros and users will add more. An unknown macro must
produce a diagnostic naming it exactly, and the macro table must be user-extensible.
Never fail silently.
