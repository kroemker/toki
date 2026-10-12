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
