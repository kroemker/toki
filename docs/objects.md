# Objects

How an object folder becomes something the viewport draws: what an object holds, how its
assets are told apart, and how a skeleton is posed and drawn the way an actor draws it.

## What an object is

An object is a file the game loads into segment 6 for the actors that use it, and
`gameplay_keep` is the one loaded into segment 4 for every scene. It is not a model. Most
creature and humanoid objects are mostly one skeleton, its textures and its animations,
but plenty are a bag of display lists the actor's code picks from, and `gameplay_keep`
holds a thousand assets of every kind: effect meshes, Link's shared animations, collision,
textures. What an actor draws of its object is decided in its C, and is not read here yet.

So an object is read as a catalog first, and what is drawn is picked from it.

## Files to images

`hackeroot::findObjectFiles` takes the object folder's `.c` files that are not `.inc.c`
pieces, which the main file includes, and its headers. `toki::object::readObject` reads
them with the `AssetImageReader` as three images: the game code (segment 0, as for a
scene), `gameplay_keep` (segment 4), and the object (segment 6, or 4 for the keep itself).
Objects point into `gameplay_keep` freely, so it is always in the image.

The game code and `gameplay_keep` are shared images (`hackeroot::SharedImageFiles`): they are
read once for the workspace and handed to every read after it, by any reader, until one of
their files changes or an image they were laid out against is read again. An object is
then only its own files. Twenty objects in a row read in 1.2 s from a copy of the checkout
on the Windows filesystem, release build: 0.5 s for the first, with the code and the keep,
and 35 ms each for the rest, where each used to cost the whole half second.

Objects point into other objects too (Link's bottle into the bottle's object), and player
animations point into data that is not asset C at all. Such a symbol is a warning, not an
error: the image is still good, and what points there draws nothing
([memory-image.md](memory-image.md#addresses-are-relocations)).

`toki::object::ObjectLoader` reads on a worker and reads again when a file changes, as the
scene loader does; both are `toki::assets::WatchedAssetReader`.

## The catalog

Every symbol in the image carries its declared type (`z64::ImageSymbol::type`), and
`listObjectAssets` sorts them by it: `SkeletonHeader` and `FlexSkeletonHeader` are
skeletons, `AnimationHeader` animations, `LinkAnimationHeader` player animations, `Gfx`
display lists, `Vtx` vertices, the limb structs limbs, `CollisionHeader` collision.
Textures and palettes are integer arrays, told apart by name (`TLUT` is a palette), since
their type says only how wide a texel is.

## Posing a skeleton

`readSkeleton` follows the header's limb table. A flexible skeleton is told by the type
of the symbol at its address, since the bytes of the two headers start alike. Standard and
LOD limbs read their joint position, child, sibling and display list; skin limbs draw
nothing yet.

`readAnimation` reads an `AnimationHeader`. Its arrays carry no length, so they are as long
as the arrays laid out at their addresses. `makeJointTable` takes one frame as
`SkelAnime_GetFrameData` does: a joint index at or past `staticIndexMax` takes the value for
this frame, any other one value for the whole animation. Entry 0 is the root's translation,
the rest each limb's rotation.

`poseSkeleton` walks the limb tree as `SkelAnime_DrawFlexLod` does, depth first, child
before sibling, each limb `Matrix_TranslateRotateZYX`ed from its parent's matrix: the root
by the frame's translation, every other limb by its joint position, then Z, Y and X turns
in binary angles. Limbs with a display list are drawn in that order.

## Drawing it

`makeObjectContent` builds what an actor's draw does:

- the default draw config and `sSetupDL[SETUPDL_25]`, from the checkout's code, as
  `Gfx_SetupDL_25Opa` sets them up;
- for a skeleton, every drawn limb's matrix as an `Mtx` in segment 13, where a flexible
  skeleton's limb display lists load them from (`gSPSegment(0x0D, mtx)` in
  `SkelAnime_DrawFlexOpa`), and a display list that loads each one before calling its
  limb's list;
- for a display list on its own, just the list.

It is lit by a fixed light from above front, without fog. `findDisplayListBounds` follows
a display list's vertex loads and calls for a box to frame the camera on; for a skeleton
each limb's box is moved by its matrix.

## Showing it

The Object panel lists the catalog by kind. Skeletons, animations and display lists can be
picked; the rest is listed with its type, address and size. An object opens on its first
animation playing on the first skeleton it fits (one joint more than the skeleton has
limbs), paused on its first frame, or failing that its first skeleton at rest, or its first
display list. Play runs at the game's 20 frames a second times a display speed, the frame
slider stops and scrubs. An object read again keeps what was picked, by name, and the
camera.

One thing is open at a time: opening an object closes the scene, and the other way round,
and the closed one's loader stops watching its files.

## Not yet

- Segments 8 to 12, which actor code points at eye and mouth textures and other per-instance
  data, are left as the default draw config sets them: the empty display list.
- Link's animations (`LinkAnimationHeader`) live in `link_animetion`, not in asset C.
- Skin limbs.
- What an actor draws of its object, which needs its C read.
