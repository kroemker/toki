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

Actor files are preprocessed as scene files are: lazily, over the header scrape, with
`#if`s answered under the build's configuration. The calls matched are functions, not
macros, so nothing needs expanding.

### The rules

The rules are Zelda-specific, so they live in `app/actor/domain/`.

- **Which functions.** The profile's initializer names `Init` and `Draw` (`EnAni_Init`,
  `EnAni_Draw`). The rules search those functions and the static functions they call
  directly in the same file, one level deep. That is a heuristic. It covers setup helpers
  such as `EnXxx_InitSkel` without following everything.
- **Skeleton.** The first `SkelAnime_Init*` call whose skeleton argument is `&symbol`,
  possibly behind a cast. Flexible or not is checked against the symbol's declared type,
  which the object catalog already knows. The name of the function called is not trusted
  for this.
- **Animation.** The init's animation argument when it is a literal. If it is `NULL`, the
  first `Animation_*` call in the same function that has a literal animation. If there is
  neither, the skeleton is drawn at rest. Whether the animation plays is a setting
  (see [Animation](#animation)).
- **Display lists.** In `Draw`, every `Gfx_DrawDListOpa` / `Gfx_DrawDListXlu` call and
  every `gSPDisplayList(POLY_OPA_DISP++ | POLY_XLU_DISP++, symbol)` with a literal symbol,
  in source order. The pass comes from the call. Calls under a branch are all taken, so a
  `Draw` that picks one display list of three draws all three. The panel says so.
- **Scale.** The last literal `Actor_SetScale` in `Init`, else the `InitChain`'s
  `ICHAIN_VEC3F_DIV1000(scale, ...)`, else 0.01. The `InitChain` is a file-scope array,
  so the existing initializer reader handles it.
- **Y offset.** The literal second argument of `ActorShape_Init`.

### Finding a symbol's object

A symbol is looked up in the profile's object first. If it is not there, the rules check
the object headers the overlay includes (`assets/objects/object_xxx/object_xxx.h`). The
header that declares the symbol names its object. This handles `Object_GetSlot` actors
and `gameplay_keep` without running code. A symbol found in neither is unresolved.

### Drawing

Drawing follows `Actor_Draw`:

- the room's objects are read once per scene, with the images shared as a scene's are, and
  segment 6 is pointed at the actor's object before it is drawn;
- the model matrix is `Matrix_SetTranslateRotateYXZ` on the actor's position, with the Y
  offset times scale added, and its rotation, followed by `Matrix_Scale`;
- a skeleton is posed with the existing `poseSkeleton`. Each actor's limb matrices get
  their own range of one generated segment, and `gSPSegment(0x0D, ...)` points at that
  range before the actor's limbs are drawn;
- segments 8 to 12 stay at the default draw config's empty display list, as the Object
  panel leaves them now. Eyes are missing until per-instance segments are done;
- the light is the scene's current light setting;
- actors are drawn in the scene's own opaque and translucent passes, after the rooms.

An actor that does not resolve keeps its diamond. A checkbox in the Scene panel switches
between models and markers.

### Animation

Whether placed actors are animated is a setting, **Animate actors**, under Viewport in
the Settings window, saved in `config.toml`. It is off by default, so each actor is
posed once, on frame 0 of its animation, when the scene's content is made. When it is on,
every animated actor loops its animation at the game's 20 frames a second. Each frame
poses every skeleton again and rewrites its range of the limb-matrix segment. The rooms
and the rest of the images stay as they are, so it is the same per-frame work the Object
panel does for one skeleton, done once per actor. The viewport then redraws continuously
instead of only when something changes.

### Which objects to read

The room header's object list (`SCENE_CMD_OBJECT_LIST`) is what the game loads, and an
actor whose object is not in it does not spawn. The scene reader does not read that list
yet, and it needs to. Objects reached only through an overlay's includes are read on
demand. Each object is read once per scene, however many actors use it.

## Cost

This has not been measured. Two reads are new:

- **Overlays.** Only the overlays placed in the open scene are read, which is usually a
  few dozen, with one preprocessed unit each. A model is cached per overlay and keyed on
  its files' modification times, so changing the layer or reopening the scene costs
  nothing.
- **Objects.** Every object read used to read `gameplay_keep` and the game code again.
  They are shared images now ([objects.md](objects.md#files-to-images)): twenty objects
  read in 1.2 s, 35 ms each after the first.

The target is that opening a vanilla scene with models on takes no more than twice what it
takes now. That gets measured before models are switched on by default.

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

## Expected coverage

Of the 431 overlays, 162 have a skeleton and roughly 140 of those use a literal one.
Another 169 draw display lists, most of them literal. Allowing for the overlays that do
something else, roughly two in three overlays should draw as a model. It will be more for
placed actors, since enemies, NPCs and props are the common ones and the literal cases.

Left for later, each with a known reason given:

- skeletons and display lists picked through tables indexed by params (`sDLists[params]`).
  This is the clearest next step: the placed actor's params are known, and the table is
  file-scope data;
- per-instance segments (eyes, mouths, tunic colours);
- skin limbs;
- Link, who is part of the game code and not an overlay;
- geometry built in code.

## Plan

| Step | What | Size |
|---|---|---|
| 1 | Share `gameplay_keep` and the code images across object reads, and measure a twenty-object read (done) | S |
| 2 | Read the room object list in the scene header | S |
| 3 | `cparser`: function definitions in `ParseResult`, `findCalls`, with tests | M |
| 4 | `app/actor/domain`: the rules, symbol-to-object lookup, `ActorModel`, unit tests, checkout coverage test | M–L |
| 5 | Scene drawing: per-actor segment 6 and limb-matrix ranges, the model matrix, opaque and translucent passes | M |
| 6 | Scene panel: model or marker per actor, where each answer came from, why it did not resolve; the checkbox | S |
| 7 | The Animate actors setting: per-frame posing and continuous redraw while it is on | S |

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
