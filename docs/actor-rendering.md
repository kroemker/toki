# Actor rendering

Design for drawing a scene's placed actors as what they are, instead of the diamonds
`makeActorMarkers` draws now. Nothing here is built yet. Once it is, this document is
rewritten to describe what was built, the way the other documents are.

## The problem

What an actor looks like is decided in its C, not its data. The scene tells us the actor's
ID, position, rotation and params. The actor table tells us its overlay. The overlay's
`ActorProfile` names its object. Everything else comes from function bodies:

```c
void EnAni_Init(Actor* thisx, PlayState* play) {
    ...
    SkelAnime_InitFlex(play, &this->skelAnime, &gRoofManSkel, &gRoofManIdleAnim,
                       this->jointTable, this->morphTable, ENANI_LIMB_MAX);
    ...
}

void EnAni_Draw(Actor* thisx, PlayState* play) {
    ...
    gSPSegment(POLY_OPA_DISP++, 0x08, SEGMENTED_TO_VIRTUAL(eyeTextures[this->eyeIndex]));
    SkelAnime_DrawFlexOpa(play, this->skelAnime.skeleton, this->skelAnime.jointTable, ...);
}
```

`cparser::Parser` steps over function definitions entirely, so none of this is read today.

## What the checkout's actors do

These counts are from a survey of the 431 overlays in `src/overlays/actors` of a HackerOoT
checkout. Each overlay is counted under the first rule it matches:

| How it draws | Overlays |
|---|---|
| A skeleton, set up by `SkelAnime_Init`, `SkelAnime_InitFlex`, `SkelAnime_InitLink` or `Skin_Init` | 162 |
| Display lists through `Gfx_DrawDListOpa` / `Gfx_DrawDListXlu`, no skeleton | 78 |
| Display lists through `gSPDisplayList(POLY_*_DISP++, gXxx)` in its own draw code | 91 |
| Something else: effects, invisible controllers, geometry built in code | 100 |

Across the 207 `SkelAnime_Init` and `SkelAnime_InitFlex` calls:

- The skeleton argument is a literal `&gXxxSkel` in 164 calls, often with a cast in front.
  The rest are table lookups (`sSkeletons[this->actor.params]`), parameters, or
  `SEGMENTED_TO_VIRTUAL(...)`.
- The animation argument is a literal in 146 calls and `NULL` in 59. A `NULL` is usually
  followed by an `Animation_Change` / `Animation_PlayLoop` with a literal a few lines later.
- 28 overlays call the init more than once, as bosses and cutscene actors with several
  bodies do.
- 54 overlays use `Object_GetSlot` to load objects besides their profile's, such as
  `EnKo`'s separate legs, body and head objects.
- 11 use skin limbs (`Skin_Init`): the horses and a few cutscene actors.

Some data an actor needs for drawing:

- **Scale.** `Actor_SetScale` takes a literal in 290 of 401 calls, and 138 overlays set
  the scale in their `InitChain` with `ICHAIN_VEC3F_DIV1000(scale, ...)`. `Actor_Init`
  sets 0.01 before any of that runs.
- **Height offset.** `shape.yOffset` from `ActorShape_Init`, applied as
  `Actor_Draw` applies it.
- **Per-instance segments.** 179 overlays point segments 8 to 12 at eye and mouth textures
  or colours in their draw code.

## Approach

Read each placed actor's overlay statically, match a small set of call patterns, and keep
only answers that are literal. Every pattern that does not resolve gives a reason, and that
actor keeps its marker. Actor code is never run.

The result for one actor is an `ActorModel`:

- the object, from the profile, resolved through the object table to its folder;
- what is drawn: a skeleton, flexible or not, with an animation, or a list of display
  lists, each for the opaque or the translucent pass;
- scale and Y offset;
- where each answer came from, as file and line, shown in the Scene panel;
- the reasons for anything unresolved.

### What `cparser` needs

Both of these are general C, and both stay out of Zelda:

1. **Function definitions.** `Parser` already finds a function's body to step over it
   (`findStatementEnd`, `skipBalanced`). `ParseResult` gains one entry per definition: the
   function's name and the token span of its body. Declarations are read exactly as before.
2. **Call sites.** `findCalls(body, names)` lists every call to the named functions in a
   body: the callee, each argument's token span split at top-level commas, and the
   location. It does no data flow. An argument is just tokens, which the caller evaluates
   with the existing constant evaluator or gives up on.

Actor files are preprocessed as scene files are, over the header scrape, with `#if`s
answered under the build's configuration (`hackeroot::preprocessOverlay`). The overlay's own
headers and the asset headers it includes are read; the game's headers are not. The calls
matched are functions, not macros, so nothing needs expanding.

### The rules

The rules are Zelda-specific, so they live in `app/actor/domain/`.

- **Which functions.** The profile's initializer names `Init` and `Draw` (`EnAni_Init`,
  `EnAni_Draw`). The rules search those functions and the functions of the main file they
  call directly, one level deep. That is a heuristic. It covers setup helpers such as
  `EnXxx_InitSkel` without following everything. A profile whose `Draw` is `NULL` draws
  with the first function the overlay assigns to `actor.draw` anywhere, since an actor that
  waits for an object to load sets it once the object is there.
- **Skeleton.** The first `SkelAnime_Init*` call whose skeleton argument is `&symbol`,
  possibly behind a cast. Flexible or not is checked against the type the object header
  declares the symbol with. The name of the function called is not trusted for this. An
  actor whose `Init` sets up no skeleton is searched whole, for the same reason as its
  `Draw`: `En_Ko` sets up its skeleton in an action function, once its objects have loaded.
- **Animation.** The init's animation argument when it is a literal. If it is `NULL`, the
  first `Animation_*` call, in `Init`'s functions or the one the skeleton is set up in, whose
  second argument is an `AnimationHeader` of an object. An `AnimationInfo` table is not one.
  If there is neither, the skeleton is drawn at rest. Whether the animation plays is a
  setting (see [Animation](#animation)).
- **Display lists.** In `Draw`, every `Gfx_DrawDListOpa` / `Gfx_DrawDListXlu` call and
  every `gSPDisplayList(POLY_OPA_DISP++ | POLY_XLU_DISP++, symbol)` with a literal symbol,
  in source order. The pass comes from the call. Calls under a branch are all taken, so a
  `Draw` that picks one display list of three draws all three. The panel says so.
- **Scale.** The last literal `Actor_SetScale` in `Init`, else the `InitChain`'s
  `ICHAIN_VEC3F_DIV1000(scale, ...)`, else 0.01. The `InitChain` is a file-scope array,
  so the existing initializer reader handles it.
- **Y offset.** The literal second argument of `ActorShape_Init`.

A name that is not an object's symbol is one of three gaps: the overlay's own data (declared
in a folder named as the overlay's is, its own or its extracted assets'), a file-scope
symbol no object header declares, or a local variable.

### Finding a symbol's object

A symbol is looked up in the profile's object first. If it is not there, the rules check
the object headers the overlay includes (`assets/objects/object_xxx/object_xxx.h`). The
header that declares the symbol names its object. This handles `Object_GetSlot` actors
and `gameplay_keep` without running code. A symbol found in neither is unresolved.

### Drawing

`toki::scene::readSceneActors` runs on a worker of its own, `SceneActorLoader`, started once
the scene has arrived, so the scene shows with markers first and its actors turn into models
when their objects have been read. It reads
the model of every actor ID placed in any layer, rooms and transition actors both, the
objects those models name, and poses each skeleton once, on frame 0 of its animation
(`SceneActors`). Objects are shared images over the game code and `gameplay_keep`, so a
reload reads only what changed. `gameplay_keep` is in segment 4, the scene's keep object in
segment 5, every other object in 6.

`makeSceneContent` then draws each placed actor as `Actor_Draw` does (`makeActorDraws`):

- one small image per actor, in segment 13: a skeleton's limb matrices first, in the order
  the limbs draw, as `SkelAnime_DrawFlexOpa` lays them out, then the list that loads each
  one and calls its limb; or the actor's matrix and one list per display list;
- the model matrix is `Matrix_SetTranslateRotateYXZ` on the actor's position, with the Y
  offset times scale added, and its rotation, followed by `Matrix_Scale`. It is multiplied
  into each limb's matrix on the CPU;
- each list starts with `sSetupDL[SETUPDL_25]` and ends by loading the identity, since the
  rooms drawn after it assume the modelview is the identity;
- before the draw, the actor's object is pointed at by its segment (`ViewportDraw::
  alsoImages`), and segments 8 to 12 at what `Draw` points them at, or else at `gEmptyDL`
  (`ViewportDraw::pointedSegments`, resolved to physical addresses when the frame runs);
- opaque draws follow the rooms' opaque pass, translucent ones the rooms' translucent pass;
- the light is the scene's current light setting.

**Segments 8 to 12.** `Draw` points them at eye and mouth textures and other per-instance
data. A `gSPSegment(POLY_OPA_DISP++ | POLY_XLU_DISP++, 0x08..0x0C, x)` is read where `x`
is a symbol, or a table the call indexes (`sEyeTextures[this->eyeIndex]`), at file scope or
static in the function: then its first entry, which for eyes is the open one by the
decomp's convention, a default state as frame 0 is. The opaque and the translucent pass
each have their own setting, as the game's two display buffers do. A display list takes
what the calls before it in its function set, since an effects actor such as
`Object_Kankyo` points segment 8 at a different texture before each of its lists; a
skeleton takes the first setting of each segment.

A symbol is also checked against its role by its declared type: a display list must be a
`Gfx`, a skeleton a `SkeletonHeader` or `FlexSkeletonHeader`, an animation an
`AnimationHeader`.

An actor without a drawing keeps its diamond. `SceneView::drawsModels` switches between
models and markers.

The Scene panel marks each placed actor with a cube when it is drawn as its model and a
diamond when it is marked, and its tooltip says what it is drawn as and where in the overlay
each answer was read, and every gap and drawing diagnostic (`describeActor`). An actor whose
profile names an object that is neither a keep object nor in its room's object list is said
not to be spawned by the game there. **Show actors** and **Draw models** are saved with the
session. The overlays and objects read are watched with the scene's own files,
so editing an actor's C or re-exporting its object reads the actors again. A scene read
again keeps the models it had until the new ones arrive. The actor loader keeps its own
image cache, apart from the scene loader's.

### Animation

Whether placed actors are animated is a setting, **Animate actors**, under Viewport in
the Settings window, saved in `config.toml` as `viewport.animate_actors`. It is off by
default, so each actor is posed once, on frame 0 of its animation, when the scene is read.
When it is on, the Scene panel counts frames at the game's 20 a second into
`SceneView::actorFrame`, and every actor whose skeleton and animation were read
(`ActorAnimation`) is posed again on that frame, wrapped to its animation's length, as its
image is written. The rooms and the objects stay the images they are; only the small
per-actor images are made again, and the viewport takes them as the next frame of the same
content (`ViewportPanel::setNextFrame`), so what it already reported is not reported on
every frame. The viewport then redraws continuously instead of only when something
changes. The Object panel's playback does the same.

### Which objects to read

The room header's object list (`SCENE_CMD_OBJECT_LIST`) is what the game loads, and an
actor whose object is not in it does not spawn. `readRoomHeader` reads it, and
`readSceneHeader` reads the scene's keep object (`SCENE_CMD_SPECIAL_FILES`), which the
game loads into segment 5 and some profiles name. Objects reached only through an
overlay's includes are read on demand. Each object is read once per scene, however many
actors use it.

## Cost

Two reads are new:

- **Overlays.** Only the overlays placed in the open scene are read, a few dozen, with one
  preprocessed unit each. They are read again on every read of the scene; at a few
  milliseconds each, a cache per overlay has not been needed yet.
- **Objects.** Shared images ([objects.md](objects.md#files-to-images)): `gameplay_keep`
  once, then each object once, until its files change.

Measured from a copy of the checkout on the Windows filesystem, release build:

| Scene | Actors drawn | Objects | Scene read | Actors read after it |
|---|---|---|---|---|
| `spot04` (Kokiri Forest) | 20 of 33 | 20 | 0.52 s | 1.36 s |
| `ydan` (Deku Tree) | 14 of 22 | 10 | 0.71 s | 0.71 s |

The target was that a first read with models costs no more than twice one without. Read in
one go, Kokiri Forest took about three and a half times as long, most of it `gameplay_keep`
and the twenty objects. So the actors are read after the scene, on their own worker: the
scene shows as fast as before, and its models follow. A second read of the actors, as after
an export, finds the objects kept.

## Testing

- **Unit tests**, no checkout: `findCalls` and function definitions in `cparser` on
  hand-written C, and each rule in `app/actor/domain` on short overlay snippets, including
  every unresolved reason.
- **A checkout test** that reads every overlay in the checkout's actor table, vanilla or
  added, and reports how many resolve to a skeleton, how many to display lists, and how many
  do not resolve, by reason. It asserts two things that hold in any checkout: no overlay
  fails to read, and a fixed list of vanilla actors (a Deku Baba, a sign, a pot, an NPC with
  a `NULL` init animation, an `Object_GetSlot` actor) resolve to what they are known to
  draw. Exact totals are not pinned, since they change whenever a user adds an actor.
- **A pixel check in the viewport** on a few known actors (a Deku Baba, a sign, a pot)
  against the Object panel showing the same skeleton or display list.

## Coverage

The estimate was two in three overlays. Measured over the 433 actors of a HackerOoT
checkout's actor table (release build, from a copy on the Windows filesystem), all read in
0.9 s:

| Result | Actors |
|---|---|
| A skeleton | 149 |
| Display lists only | 161 |
| No model | 123 |

That is 72%. The gaps behind the 123, and behind models that are partly read:

| Gap | Count |
|---|---|
| Display list not a symbol (tables, locals, `this->dList`) | 76 |
| The overlay's own data (effects, ocarina songs, the Arwing) | 55 |
| Animation not a symbol (skeleton drawn at rest) | 33 |
| No `Draw` anywhere | 31 |
| Nothing drawn by name in `Init` or `Draw` | 27 |
| Scale not a constant | 20 |
| Skin skeleton | 11 |
| Skeleton not a symbol | 9 |
| No overlay (Player, `En_Item00`, `En_A_Obj`) | 3 |

Placed actors should do better than the table as a whole, since enemies, NPCs and props
are the common ones and the literal cases.

Left for later, each with a known reason given:

- skeletons and display lists picked through tables indexed by params (`sDLists[params]`).
  This is the clearest next step: the placed actor's params are known, and the table is
  file-scope data;
- per-instance segments set to anything but a symbol or a table of them: colours built in
  code, scrolling textures, matrices;
- skin limbs;
- Link, who is part of the game code and not an overlay;
- geometry built in code.

## Plan

| Step | What | Size |
|---|---|---|
| 1 | Share `gameplay_keep` and the code images across object reads, and measure a twenty-object read (done) | S |
| 2 | Read the room object list in the scene header (done) | S |
| 3 | `cparser`: function definitions in `ParseResult`, `findCalls`, with tests (done) | M |
| 4 | `app/actor/domain`: the rules, symbol-to-object lookup, `ActorModel`, unit tests, checkout coverage test (done) | M–L |
| 5 | Scene drawing: per-actor segment 6 and limb-matrix ranges, the model matrix, opaque and translucent passes, segments 8 to 12 (done) | M |
| 6 | Scene panel: model or marker per actor, where each answer came from, why it did not resolve; the checkbox (done) | S |
| 7 | The Animate actors setting: per-frame posing and continuous redraw while it is on (done) | S |

S is about a session, M two or three, L more. Steps 1 to 3 are independent of each other.
Step 4 needs step 3, and step 5 needs steps 1, 2 and 4. After step 4, the checkout test gives
real coverage numbers before anything is drawn. If they come in well under the estimate
above, that is the point to stop and rethink.

## Decided

- **No overrides for now.** A user cannot correct an actor the rules get wrong yet. When
  they can, it is through the actor database ([Later](#later-an-actor-database)), not an
  ad hoc table in `config.toml`.
- **Animation is a setting, off by default** ([Animation](#animation)).
- **Added actors are vanilla actors.** Nothing in the rules or the drawing tells an actor
  the user wrote from one the decomp ships. Each is read through the actor table and its
  overlay folder the same way, and the checkout test covers whatever the checkout has.

## Later: an actor database

Some of what an editor needs about an actor is not practical to parse out of its C. The
clearest case is what its params mean: which bits are a switch flag, which are a type, and
what each value is called. That information belongs in a data file, TOML or JSON, keyed
by actor, that ships with the tool and that a user can extend for their own actors.

Once that file exists, it is also where the skeleton and default animation shown for an
actor can be set. What is read from the C stays the default. An entry in the database
overrides it and is shown as an override in the Scene panel, with the value read from the
C next to it, so the two cannot drift apart unnoticed. The rules in this document are what
fill most of the database for free. It is not designed here.

## Ruled out: running actor code

The alternative to matching calls is running each actor's `Init` and `Draw` once and taking
the display list it writes, interpreting the C against the memory image. It is ruled out
for now, for these reasons:

- **Size.** It needs a C interpreter: every statement, full expressions, pointers into the
  image at the target's layouts, function pointers, and single-precision float math. It
  also needs a game library. Actor overlays call 592 distinct functions defined in
  `src/code`, which is 138 files and about 100,000 lines. Some of that code can itself be
  interpreted (`sys_matrix.c`, `z_skelanime.c`, `z_lib.c`). The rest (collision, audio,
  `Actor_Spawn`, `DynaPoly`, effects) would need stand-ins, with the boundary decided
  function by function. That is several times the static plan.
- **Performance.** Every placed actor would run interpreted C on every read and every
  reload. Animating would mean running `Update` and `Draw` every frame. Pattern matching is
  one pass over tokens that are already preprocessed.
- **The world is not there.** 124 overlays check switch flags, 65 check Link's age, and 25
  raycast to the floor in `Init`. 304 call `Actor_Kill` somewhere. `Draw` often reads state
  that `Update` sets. What running the code shows depends on a world the tool does not
  simulate.
- **Running the compiled MIPS instead** needs a build and a CPU emulator. That breaks the
  rule of reading the source tree without a build.

What it would gain is real: eye and mouth segments, choices made by params, parts placed by
computed matrices, and display lists built in code. Coverage would likely rise from about
two in three to nine in ten. Step 3 of the plan, function bodies in `cparser`, is what an
interpreter would build on, and the memory image keeps the option open. The idea is
revisited once the static rules' unresolved reasons show what is left.
