# The memory image

How asset C becomes what the game holds in memory: per-segment buffers, the way
`gSPSegment` maps a scene to segment 2, a room to segment 3 and an object to segment 6,
with every address patched in. The renderer reads display lists straight out of it.

The target is not "close": every vanilla scene, room and object, and the checkout's own
fast64 exports, must come out byte for byte as the build compiles them. A checkout test
holds that against the build's own object files.

## The pipeline

`hackeroot::readAssetImage` takes the C files of each segment and runs, per file:

1. **Read the file** as asset reading always does (`preprocessAsset`): includes followed
   only into the asset trees, textures resolved to the files the build generates them
   from. What it could not follow is in `TranslationUnit::unresolvedIncludes`: the system
   headers it includes.
2. **Read those headers as a prelude** (`hackeroot::readPrelude`), the way the compiler
   reads them: against the configuration alone rather than the scrape, every condition
   answered, every header they include read too, and macros expanded as the text is read
   (`PreprocessOptions::expandMacros`), so a macro defined, used by an X-macro table and
   undefined again expands as it did there. Preludes are cached by their include list;
   most files of a scene share one.
3. **Lay out the types** the prelude defines (`cparser::parseTypes`): typedefs, structs,
   unions, enums and bitfields, sized and aligned as MIPS o32 GCC does. The file's own
   headers are read for types too, which is where an object's limb enumerators live.
4. **Type every declaration** from its written type, pointers and array bounds. Two C
   rules decide what an object is, and both change bytes:
   - an array declared `[]` takes its length from an earlier declaration of the same name
     when there is one (`extern Gfx gLinkChildBottleDL[33];` makes the later
     `Gfx gLinkChildBottleDL[] = { three commands }` thirty-three commands long);
   - an object sits where it was first declared other than `extern`. A tentative
     definition reserves the place and a later one with an initializer fills it, as GCC
     with `-fno-toplevel-reorder` places it.
5. **Encode every object** (`z64::DataEncoder`): the initializer fills its type as C fills
   it, braces elided, designators jumping, the rest zero, scalars big-endian, reals IEEE
   754, bitfields from the most significant bit, embedded files as the build generates
   them. A `Gfx` array goes through `z64::DisplayListEncoder` instead.

Then `z64::layOutImage` places the objects in order, each segment from zero, every object
at its alignment (aggregates at least four bytes, as MIPS GCC aligns them) and every file
padded to sixteen, and patches the addresses.

## Addresses are relocations

An object is encoded before anything is laid out, so an address cannot be known while its
initializer is evaluated. The evaluator carries, on every value derived from a symbol's
address, which symbol it was (`cparser::Value::symbol`): adding and subtracting keep it,
casts that keep 32 bits keep it, anything else drops it and says so. A word written from
such a value is a relocation, and the word already holds the constant added to the
address, as a MIPS object file keeps the addend in place for `R_MIPS_32`. Patching adds
the symbol's segmented address.

That is also why the bytes can be checked: unpatched, a segment is exactly the `.data` of
the object file the build made from the same C.

Symbols nothing in the image defines:

- **The linker's ROM symbols** (`_spot00_room_0SegmentRomStart`) read as zero. Toki64 reads
  no ROM.
- **The game code's data** that assets point at, such as `gIdentityMtx`, is written down in
  `z64::getCodeSymbols` with its contents and placed in segment 0, which the game reads
  physical addresses through. Code whose data a viewer needs whole, such as the setup
  display lists in `z_rcp.c`, is read like an asset file into segment 0 instead: the
  parser steps over its functions, and an array of display lists (`Gfx sSetupDL[71][6]`)
  encodes each inner list.
- **Anything else**, an object's reference into gameplay_keep when gameplay_keep is not in
  the image, is a warning naming the symbol. Objects do point into one another (Link's
  bottle into the bottle object) and into data that is not asset C at all (player
  animations into `link_animetion`), so an image without them is still a good image.

## Several images

The game loads a scene into segment 2 and then each room in turn into segment 3, so a
scene's rooms cannot share one image: they would all claim segment 3.
`hackeroot::AssetImageReader` reads several images in one pass, each laid out on its own,
and a symbol one image does not define resolves to where an earlier image laid it out, so a
room's palette in its scene is found. It reads again and again against one workspace and
keeps the preludes between reads, which are most of the cost of reading a fast64 export.
Images that are under every read, the game code and `gameplay_keep`, can be asked for as
shared images, each with the files it watches: one is read again only when a watched file
changed or an image before it was read again, and is otherwise the same image the last read
made. Everything else, the files, their textures and the folders they list, is read afresh
every time.

## Rendering from it

`z64::mapImage` maps every segment into an `n64::Memory` and returns the segment table,
which is an `n64::RspTask` together with a display list's segmented address
(`MemoryImage::findAddress`). `z64::mapImageAt` maps one image after another into the same
memory. Segment 0 always maps at physical address 0, so an address in the game's code is
physical, as on the N64, and a `gsSPSegment(0x08, gEmptyDL)` in code data points where it
should.

## Measured

Against a HackerOoT checkout over `\\wsl.localhost`, release build: a room's prelude, 71
headers and 479 typedefs, reads in 0.36 s. Every scene and room, 500 files, encodes and is
compared with its object file in under four minutes, most of it reading over the share.
