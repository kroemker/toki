#ifndef ULTRA64_GBI_H
#define ULTRA64_GBI_H

#include "config.h"

#ifdef F3DEX_GBI_2
#define G_VTX 0x01
#define G_TRI2 0x06
#define G_ENDDL 0xDF
#define G_SETTIMG 0xFD
#define G_IM_SIZ_16b 2
#define G_IM_SIZ_16b_LOAD_BLOCK 2
#else
#define G_VTX 0x04
#define G_ENDDL 0xB8
#define gsSPOldOnly() { 0xB8, 0 }
#endif

#define _SHIFTL(v, s, w) ((unsigned int)(((unsigned int)(v) & ((0x01 << (w)) - 1)) << (s)))

#define gsDma1p(c, s, l, p) \
{                           \
   (_SHIFTL((c), 24, 8) |   \
    _SHIFTL((p), 16, 8) |   \
    _SHIFTL((l), 0, 16)),   \
    (unsigned int)(s)       \
}

#ifdef F3DEX_GBI_2
#define gsSPVertex(v, n, v0) \
    { (_SHIFTL(G_VTX, 24, 8) | _SHIFTL((n), 12, 8) | _SHIFTL((v0) + (n), 1, 7)), (unsigned int)(v) }
#define gsSP2Triangles(a, b, c, f0, d, e, f, f1) \
    { (_SHIFTL(G_TRI2, 24, 8) | _SHIFTL((a) * 2, 16, 8) | _SHIFTL((b) * 2, 8, 8) | _SHIFTL((c) * 2, 0, 8)), \
      (_SHIFTL((d) * 2, 16, 8) | _SHIFTL((e) * 2, 8, 8) | _SHIFTL((f) * 2, 0, 8)) }
#define gsSPEndDisplayList() { _SHIFTL(G_ENDDL, 24, 8), 0 }
#define gsSetTextureImage(siz, i) { _SHIFTL(G_SETTIMG, 24, 8) | _SHIFTL(siz, 19, 2), (unsigned int)(i) }
#define gsDPLoadTwice(siz, i) gsSetTextureImage(siz##_LOAD_BLOCK, i), gsSetTextureImage(siz, i)
#define gsImmp1(c, p0) { _SHIFTL((c), 24, 8), (unsigned int)(p0) }
#define gsSPRectangle(x, s) _SHIFTL(0xE4, 24, 8) | (x), 0, gsImmp1(0xE1, s)
#endif

#endif
