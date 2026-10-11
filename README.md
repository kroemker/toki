# Toki64

A desktop workbench for modding The Legend of Zelda: Ocarina of Time on top of the
[HackerOoT](https://github.com/HackerN64/HackerOoT) decompilation.

Toki reads the HackerOoT source tree directly, with no ROM and no build, and renders,
inspects and edits what is in it. It aims to be what decomp modding lacks: an IDE that
knows the game.

## What it does

- **Reads a checkout as the build does.** A C front end with a real preprocessor, the
  Makefile and `config.h` answering every `#if`, and the checkout's actor, object, scene
  and entrance tables. Files are watched and read again on change, off the UI thread.
- **Renders scenes and objects with its own N64 renderer.** Asset C becomes a memory image
  with segment addressing, byte for byte as the build compiles it. Display lists run
  through an F3DEX2 RSP and RDP model on the CPU, then a single ubershader draws them
  through SDL3's GPU API (D3D12, Vulkan, Metal).
- **Draws actors as their real models, not cubes.** Each actor's overlay C is read for
  its object, skeleton, animation, display lists, scale and params, from literal calls in
  `Init` and `Draw`, without running any code. What it finds is kept in an editable actor
  database per checkout, seeded from Toki's own.
- **Knows what params mean.** Params are named bit ranges: numbers, flags, or choices
  whose values come from the checkout's enums (`GetItemID`, `SfxId`, `NA_BGM`, the actor
  and scene tables) or the actor's own lookup tables. Variants pick what an actor draws by
  its params, age and scene.
- **Links actors by their scene flags.** Switch, treasure, collectible and clear flags are
  derived from the `Flags_*` calls. The Scene panel lists who reads and sets each flag,
  and the viewport draws an arrow from a floor switch to the door it opens.
- **Drives the build** under WSL, with the output decoded.

Geometry editing, an embedded emulator and ROM parsing are out of scope for now.

## Requirements

- CMake 3.28+ and Ninja
- A C++23 compiler. On Windows, MSVC 19.40+ (Visual Studio 2022 17.10 or Build Tools).
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set

```
git clone https://github.com/microsoft/vcpkg.git D:/dev/tools/vcpkg
D:/dev/tools/vcpkg/bootstrap-vcpkg.bat
setx VCPKG_ROOT D:\dev\tools\vcpkg
```

Dependencies (SDL3, SDL_shadercross, Dear ImGui, glm, toml++, libspng, Catch2) are declared in
`vcpkg.json` and installed by CMake on first configure.

## Build

The Ninja generator needs the MSVC environment on PATH, so either run from an
**x64 Native Tools Command Prompt** or let your IDE supply the toolchain.

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The executable lands in `build/debug/toki.exe`. Open a HackerOoT checkout from the
Workspace panel; it may live in WSL.

There are two test binaries: `toki_tests` needs no GPU, and `toki_gpu_tests` compares the
ubershader against the CPU reference and skips without a GPU. The tests that read a real
HackerOoT checkout skip unless one is configured. To run them against a checkout in WSL,
copied to the Windows filesystem first because every file open over `\\wsl.localhost`
costs milliseconds:

```
tools\run_checkout_tests.ps1 -Distro Ubuntu -Checkout ~/HackerOoT
```

## CLion

CLion configures by calling `cmake` directly rather than through `CMakePresets.json`, so
two things need setting once:

- **Settings → Build, Execution, Deployment → Toolchains**: use **Visual Studio**, not
  MinGW. vcpkg installs MSVC-ABI packages for the `x64-windows` triplet; a MinGW toolchain
  configures fine and then fails at link.
- `VCPKG_ROOT` must be set in the environment CLion inherits. If it was set after CLion
  started, restart CLion — `CMakeLists.txt` reads it to locate the vcpkg toolchain when no
  preset is used.

Alternatively enable **Settings → Build, Execution, Deployment → CMake → Presets** and use
the `debug` profile, which carries both the toolchain file and the compiler pin.

## Layout

```
src/
  base/        Width aliases and diagnostics, shared by everything.
  cparser/     A C front end: lexer, preprocessor, macros, constant expressions,
               declarations, X-macro tables. Knows nothing of Zelda.
  makeparser/  A GNU make reader.
  z64/         Zelda64 formats and decomp conventions: dialect, texture and gbi
               encoding, the memory image.
  hackeroot/   The user's checkout: discovery, tables, config, build runner.
  n64/         The hardware: F3DEX2 on the RSP, RDP state, texture memory. CPU only.
  gfx/         The GPU side of the renderer: device, ubershader, render targets.
  app/         Features (core, workspace, assets, build, viewport, actor, scene,
               object, texture, ...), each split into ui/ and domain/.
tests/         Catch2 tests and fixture checkouts.
docs/          How each part works.
```

`domain/` never includes `ui/`, so everything that parses is testable without SDL, ImGui
or a GPU. Namespaces are `base::`, `cparser::`, `makeparser::`, `z64::`, `hackeroot::`,
`n64::`, `gfx::`, and `toki::<feature>` for anything under `app/`.

## Documentation

- [CLAUDE.md](CLAUDE.md): architecture, dependency rules and code style
- [docs/](docs/README.md): reading C, build configuration, the memory image, the renderer,
  scenes, objects, textures, the actor database and actor rendering
- [WORK_PACKAGES.md](WORK_PACKAGES.md): what is done and what is planned
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
