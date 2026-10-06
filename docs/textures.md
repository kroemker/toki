# Textures

How the textures of an open object or scene are found in its memory image, paired with
their palettes, decoded and shown.

## Where a texture's format comes from

A texture in the decomp is an integer array filled by an include the build generates,
`#include "gEyeTex.ia8.inc.c"`, which `resolveTextureInclude` resolves to the PNG it is
generated from ([memory-image.md](memory-image.md)). The array's type says only how wide
an element is. The file name says the rest: format, texel size and, for a colour-indexed
image, which palette it uses.

So when an embedded image or palette fills an object, `hackeroot::readAssetImage` records
what it made on the object and its symbol (`z64::ImageTexture`): format and size from the
name as the build reads it, width and height from the PNG the encoder decoded, and a
palette name. A colour-indexed image's palette name is the shared palette it names
(`tlut_gWallTLUT`), or else its own prefix; a palette's is the same, read from the image
its include resolved to. Matching names pair an image with its palette. The object
catalog takes texture and palette kinds from this too, not from symbol names.

## Decoding

`toki::texture::decodeTexture` reads a texture's bytes out of the image and decodes them
the way the RDP decodes each format (`n64::unpackTexels`): channels widened by repeating
their top bits, IA with its own alpha, I with the intensity as alpha. A colour-indexed
texture looks its indices up in its RGBA16 palette, the high half of a split palette
taken back down by 128. A palette that cannot be found shows the indices as grey and says
so; it is looked for in the texture's own image first and then in the others listed with
it, as a room's textures may use its scene's palettes.

What is shown is the bytes the game reads, not the PNG: an encoding mistake shows.

## Showing it

The Textures panel lists every texture of the open object, or of the open scene's scene
file and rooms, as thumbnails over the one picked. The picked one shows large, at a whole
zoom factor, over a checkerboard, with its format, size, address and palette, an optional
pixel grid, and the texel under the mouse. Picking a texture in the Object panel picks it
here.

ImGui's GPU backend samples linearly and offers no other sampler, so pixels are scaled up
by a whole factor on the CPU (`makeScaledPixels`) and uploaded at the size they are drawn,
which keeps them sharp. Thumbnails and the large view are decoded and uploaded only when
first shown.

## Not yet

- Textures built or pointed at by code: segment textures such as eyes, which actor code
  sets through segments 8 to 12.
- Materials: which display lists use a texture, with which tile and combiner settings.
