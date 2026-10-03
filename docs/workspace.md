# The workspace

How the app keeps one HackerOoT checkout read and current: what a workspace holds, where
it is read, and how it notices that the checkout changed.

## What a workspace is

`hackeroot::Workspace` is everything read from one checkout under one set of build
settings:

- the build's defines, read from the Makefile (`readBuildDefines`, including `VERSION`);
- the scraped headers, with the configuration layered over them
  ([build-configuration.md](build-configuration.md));
- the ordinal tables — scenes, actors, objects, entrances — as typed entries whose `id`
  is the ID the build gives them;
- the scene and object folders, from the checkout's own `assets/` and from
  `extracted/<VERSION>/assets/`.

A change to the checkout or to the settings is a new workspace, never an edit to the old
one. The old workspace stays valid until it is dropped, which is what lets the UI keep
showing it while the new one is read. The scrape is held behind a pointer, because the
configuration's macros point at it; that is what lets a workspace move.

`findCheckoutRoot` accepts any folder inside a checkout and walks up to the directory
holding a `Makefile`, `include/config.h` and `include/tables/scene_table.h`.

## Build settings

The user's build settings are make's command line (`VERSION`, `DEBUG_FEATURES`, ...),
persisted under `[build]` in the app's `config.toml`. Empty means the Makefile's own
defaults. The workspace reads the checkout with them, and the build runner (package 6)
must build with the same ones, or the viewer and the build disagree about every ID.

## Reading off the UI thread

A read takes about two seconds over `\\wsl.localhost` in a Debug build, almost all of it
the header scrape. `toki::workspace::WorkspaceService` therefore reads on a worker
thread. The UI thread calls `open()` to ask for a read and `update()` once per frame to
take over a finished one; until then it keeps whatever workspace it had. A read that
fails is logged and leaves the current workspace in place.

## Noticing changes

**Windows delivers no file change notifications for `\\wsl.localhost`.** A
`FileSystemWatcher` on the share saw nothing while a file's modification time changed
under it, so the service polls instead. One modification-time query costs about a
millisecond there, and a workspace watches around twenty files, so polling once a second
on the worker thread is cheap.

`Workspace::listWatchedFiles` names what is polled: the Makefile and the make files it
may include (`.make_options.mk`, `.make_hackeroot.mk`, watched even when absent so that
creating one is a change), every configuration header the configuration layer read, and
the four tables. A change to any of them reads the whole checkout again.

What is deliberately not watched: the 260-odd scraped headers under `include/`. Polling
them every second would cost a quarter of a second each time, and they change rarely.
Reopening the checkout picks them up.
