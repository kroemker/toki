# Third-party notices

Code in this repository that is taken or ported from other projects, and the notices their
licenses require.

## n64texconv (ZeldaRET)

The texel packing rules in `src/n64/TexturePacking.cpp` and the texture name and palette
rules in `src/z64/TextureName.cpp` and `src/z64/TextureEncoder.cpp` are ported from the
n64texconv library and `build_from_png` in the decomp's `tools/assets`, so that textures
encode byte for byte as the build encodes them. Only the MIT-licensed library is used;
its GPL quantizer (libimagequant) is not.

```
MIT License

Copyright (c) 2025 ZeldaRET

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
