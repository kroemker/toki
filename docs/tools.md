# Tools

The tools change the checkout: Configure sets the configuration headers, Register Scene
makes a fast64 export part of the build, Create Actor writes a new actor and registers it.
They are the only part of Toki that writes the checkout's code, and they share one way of
doing it.

## Editing a checkout

Every tool works the same way, in `hackeroot/CheckoutEdits`:

- **Planned, then applied.** A tool reads the files it would change and says what it would
  make of each (`hackeroot::EditPlan`): for every file its text as read and as it would be
  written, and each item, as a person reads it, present already, to be added, or to be
  changed. The plan is shown before anything is written, and a tool run again on a
  checkout it has already changed finds everything present and writes nothing.
- **Never over a change it did not see.** `applyEditPlan` writes only when every file is
  still what the plan read, and a file it would create does not exist yet; otherwise
  nothing is written and the plan is made again. A file is written beside itself and then
  replaces itself whole, with the line endings it had.
- **Off the UI thread.** Plans are made and applied on a worker (`toki::tools::EditWorker`),
  since nothing on the UI thread reads the checkout. The workspace sees the change by its
  own polling and reads the checkout again.
- **In Toki's blocks.** What a tool adds to a table or to the spec goes into a block of its
  own, between `// Toki64 <what> — managed block` and `// Toki64 end`, so what was written
  by a tool shows at a glance. A block is made once, before a file's last lines that must
  stay last (`#if TESTSUITE_MODE`), or at its end. The markers are the Rust prototype's, so
  blocks it made are found and added to. An actor or object may instead take an unused slot
  of its table (`DEFINE_ACTOR_UNSET`), which keeps every ID after it.

The text edits it offers are each a function of a file's text and so tested on strings:
setting a `#define` on a given line, keeping its indent and comment; adding to a block;
adding or rewriting a spec segment by name; taking an unset slot; inserting a line before
another.

## Configuration

`hackeroot::readConfigDefines` lists the settings of `include/config/*.h` as the build reads
them: it reads `config.h` under the build's `-D` flags, as `readConfiguration` does, and is
told of every `#define` in a branch the conditionals take (`PreprocessOptions::onDefine`).
So a setting written in both arms of an `#if`, as `DEBUG_BUFFER_SIZE` is, is the line the
build reads, not the first one in the file. Each setting carries what it is written as
there, the comment above it, and its effective value once `config_safeguards.h` has had its
say, which overrides settings without being one; `BOOT_TO_SCENE` is forced off without
`DEBUG_FEATURES`.

## Configure

The Configure panel, in the Tools menu, has a tab for each configuration header, named
after it (`config_debug_controls.h` is "Debug Controls"), and a row for each of its
settings (`toki::tools::makeConfigGroups`): its name as words, its comment and where it is
written in a tooltip, and a control chosen by what it is written as. `true` and `false` are
toggled; a number, whole or with a point, is typed and checked; a name is picked, from a
list for the game's buttons, controller ports, ages and times of day, or from the checkout's
own collections for entrances (`ENTR_`), scenes (`SCENE_`) and music (`NA_BGM_`). Anything
else is typed as text.

A setting the safeguards override shows what the build ends with beside it. Changes are
held until "Review", which plans them (`makeConfigPlan`): one item per changed line, and a
warning for each overridden setting, since the build will not see what is written. The
panel reads the configuration again once the plan is written.

## Register Scene

fast64 writes a scene's C into `assets/scenes/<name>/` and nothing else, so the build does
not know the scene until it is named in the spec and the scene and entrance tables.
Everything else, the `SceneID` and `EntranceIndex` enums, `gSceneTable`, the ROM segment
symbols, is generated from those. The Register Scene panel lists every scene folder of the
checkout's own assets, registered or not, and plans (`makeScenePlan`):

- **The spec.** A segment for the scene, `<name>_scene`, and one for each room,
  `<name>_room_<n>`, compressed and aligned to 0x1000, linking the folder's files in the
  order fast64 and the vanilla spec list them: the file with the header first, then the
  scene's collision and cutscenes, or the room's model info and model. A segment that is
  there with other files is rewritten where it is, so a scene that gained a room or a file
  is brought up to date.
- **The scene table.** `DEFINE_SCENE(<name>_scene, none, SCENE_<NAME>, <draw config>, 0, 0)`,
  the draw config picked from the checkout's `SceneDrawConfig`.
- **The entrance table.** For each spawn, four entrances, `ENTR_<NAME>_<spawn>` and
  `_1` to `_3`, for child day and night and adult day and night: the game finds a layer's
  entrance by its offset from the first, so a group is never split.
- **The map select.** An entry ahead of the first vanilla one, Hyrule Field, under a name
  made of the folder's, `ocean_test` as "Ocean Test".
- **Booting into it**, when asked: `BOOT_ENTRANCE`, `BOOT_TO_SCENE`, `ENABLE_HACKER_DEBUG`,
  and the age and time of day if picked, on the lines the configuration reads them from,
  written last, so the build never boots to an entrance a failed write left out.

Before planning, the scene is read as the game loads it (`surveyScene`), so the spawns and
the room list come from its header's bytes. It warns of a name that is not a C identifier,
a room list that names another number of rooms than the folder has, a full scene table, a
scene enum another scene has, and booting into the scene without `DEBUG_FEATURES`, which the
safeguards answer by booting to the title screen. A scene the Rust prototype registered
reads as registered: its segments, entries and markers are the ones this writes.
