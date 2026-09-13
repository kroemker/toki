# Fast3D (not vendored yet)

This directory will hold Fast3D, extracted from
[libultraship](https://github.com/Kenix3/libultraship) (`src/graphic/Fast3D/`, MIT).

It is deliberately empty for now. Extracting it is the next work package, and it is the
main unknown in the project: Fast3D is not published standalone, and `gfx_pc` reaches into
libultraship's window, resource and ImGui layers to an extent that has not been measured
yet. Until that is done, `src/gfx/` has no sources and no `toki_gfx` target exists.

When it lands, keep the diff against upstream minimal and documented so it can be
re-merged.
