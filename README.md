# Toki64

A desktop workbench for modding The Legend of Zelda: Ocarina of Time on top of the
[HackerOoT](https://github.com/HackerN64/HackerOoT) decompilation.

See [CLAUDE.md](CLAUDE.md) for architecture, parsing strategy and code style.

## Requirements

- CMake 3.28+ and Ninja
- A C++23 compiler. On Windows, MSVC 19.40+ (Visual Studio 2022 17.10 or Build Tools).
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set

```
git clone https://github.com/microsoft/vcpkg.git D:/dev/tools/vcpkg
D:/dev/tools/vcpkg/bootstrap-vcpkg.bat
setx VCPKG_ROOT D:\dev\tools\vcpkg
```

Dependencies are declared in `vcpkg.json` and are installed by CMake on first configure.

## Build

The Ninja generator needs the MSVC environment on PATH, so either run from an
**x64 Native Tools Command Prompt** or let your IDE supply the toolchain.

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The executable lands in `build/debug/toki.exe`.

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
  z64/         Pure Zelda64/N64 formats. No filesystem, no processes, no UI.
  hackeroot/   The user's HackerOoT checkout: discovery, tables, config, build runner.
  gfx/         Fast3D binding. Empty until Fast3D is vendored.
  app/         Features, each split into ui/ and domain/.
third_party/
  Fast3D/      Not vendored yet; see its README.
```

Namespaces are `z64::`, `hackeroot::`, `gfx::`, and `toki::<feature>` for anything under
`app/`. The `ui/` and `domain/` split is a directory boundary, not a namespace level.
