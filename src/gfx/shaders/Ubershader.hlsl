// Repeats the texel fetch, combiner and blender of n64/ in the same arithmetic;
// toki_gpu_tests holds the two sides to each other.

struct Interpolants
{
    float4 position : SV_Position;
    float2 texture : TEXCOORD0;
    float4 color : TEXCOORD1;
};

#ifdef VERTEX_STAGE

cbuffer VertexUniforms : register(b0, space1)
{
    float4 viewportScale;
    float4 viewportTranslate;
    float4 screen;
};

struct VertexInput
{
    float4 position : TEXCOORD0;
    float2 texture : TEXCOORD1;
    float4 color : TEXCOORD2;
};

static const float DepthRange = 1023.0;

Interpolants VertexMain(VertexInput input)
{
    const float4 p = input.position;
    const float w = p.w;
    const float x = (p.x * viewportScale.x) + (viewportTranslate.x * w);
    const float y = (-p.y * viewportScale.y) + (viewportTranslate.y * w);
    float z = (p.z * viewportScale.z) + (viewportTranslate.z * w);
    if (viewportTranslate.w >= 0.0)
    {
        z = viewportTranslate.w * w;
    }

    Interpolants output;
    output.position = float4((x * 2.0 / screen.x) - w, w - (y * 2.0 / screen.y), z / DepthRange, w);
    output.texture = input.texture;
    output.color = input.color;
    return output;
}

#endif

#ifdef FRAGMENT_STAGE

StructuredBuffer<uint> textureMemory : register(t0, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    uint4 combiner;
    uint4 colors;
    uint4 constants;
    uint4 modes;
    uint4 tile0;
    uint4 tile1;
};

static const uint FlagTextured = 1u << 0;
static const uint FlagShade = 1u << 1;
static const uint FlagTwoCycle = 1u << 2;
static const uint FlagCopy = 1u << 3;
static const uint FlagFill = 1u << 4;
static const uint FlagAlphaCompare = 1u << 5;
static const uint FlagAlphaCompareDither = 1u << 6;
static const uint FlagAlphaCoverageSelect = 1u << 7;
static const uint FlagCoverageTimesAlpha = 1u << 8;
static const uint FlagPerspective = 1u << 9;
static const uint FlagForceBlend = 1u << 10;
static const uint FlagColorImage32 = 1u << 11;

static const uint FormatRgba = 0;
static const uint FormatColorIndex = 2;
static const uint FormatIntensityAlpha = 3;
static const uint FormatIntensity = 4;

static const uint FilterPoint = 0;
static const uint FilterAverage = 3;

static const uint TlutNone = 0;
static const uint TlutIntensityAlpha16 = 3;

struct Tile
{
    uint format;
    uint size;
    uint palette;
    uint rowWords;
    uint address;
    bool clampS;
    bool mirrorS;
    uint maskS;
    uint shiftS;
    bool clampT;
    bool mirrorT;
    uint maskT;
    uint shiftT;
    int lowS;
    int highS;
    int lowT;
    int highT;
};

Tile unpackTile(uint4 packed)
{
    Tile tile;
    tile.format = packed.x & 0x7;
    tile.size = (packed.x >> 3) & 0x3;
    tile.palette = (packed.x >> 5) & 0xF;
    tile.clampS = ((packed.x >> 9) & 1) != 0;
    tile.mirrorS = ((packed.x >> 10) & 1) != 0;
    tile.maskS = (packed.x >> 11) & 0xF;
    tile.shiftS = (packed.x >> 15) & 0xF;
    tile.clampT = ((packed.x >> 19) & 1) != 0;
    tile.mirrorT = ((packed.x >> 20) & 1) != 0;
    tile.maskT = (packed.x >> 21) & 0xF;
    tile.shiftT = (packed.x >> 25) & 0xF;
    tile.rowWords = packed.y & 0x1FF;
    tile.address = (packed.y >> 16) & 0x1FF;
    tile.lowS = int(packed.z & 0xFFF);
    tile.highS = int(packed.z >> 16);
    tile.lowT = int(packed.w & 0xFFF);
    tile.highT = int(packed.w >> 16);
    return tile;
}

int4 unpackColor(uint color)
{
    return int4(color >> 24, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

uint loadByte(uint address)
{
    address &= 0xFFF;
    const uint word = textureMemory[modes.y + (address >> 2)];
    return (word >> ((address & 3) * 8)) & 0xFF;
}

int shiftCoordinate(int value, uint shift)
{
    return shift <= 10 ? (value >> shift) : (value << (16 - shift));
}

int2 findTileCoordinate(Tile tile, float2 texels)
{
    const int2 fixedPoint = int2(floor(texels * 32.0));
    return int2(shiftCoordinate(fixedPoint.x, tile.shiftS) - (tile.lowS << 3),
                shiftCoordinate(fixedPoint.y, tile.shiftT) - (tile.lowT << 3));
}

int wrapAxis(int value, bool clampAxis, bool mirror, uint mask, int low, int high)
{
    if (clampAxis || mask == 0)
    {
        const int maximum = high >= low ? (high - low) >> 2 : 0;
        value = clamp(value, 0, maximum);
    }
    if (mask == 0)
    {
        return value;
    }
    const int size = int(1u << min(mask, 10u));
    if (mirror && (value & ((size * 2) - 1)) >= size)
    {
        return size - 1 - (value & (size - 1));
    }
    return value & (size - 1);
}

int2 wrapTexel(Tile tile, int2 texel)
{
    return int2(wrapAxis(texel.x, tile.clampS, tile.mirrorS, tile.maskS, tile.lowS, tile.highS),
                wrapAxis(texel.y, tile.clampT, tile.mirrorT, tile.maskT, tile.lowT, tile.highT));
}

int4 decodeRgba16(uint value)
{
    const uint3 five = uint3((value >> 11) & 0x1F, (value >> 6) & 0x1F, (value >> 1) & 0x1F);
    const uint3 expanded = (five << 3) | (five >> 2);
    return int4(expanded, (value & 1) != 0 ? 0xFF : 0);
}

int4 decodeIntensityAlpha16(uint value)
{
    const int intensity = int(value >> 8);
    return int4(intensity, intensity, intensity, int(value & 0xFF));
}

int4 makeGray(uint value)
{
    return int4(value, value, value, value);
}

int4 fetchTexel(Tile tile, int2 texel, uint tlut)
{
    const uint x = uint(texel.x);
    const uint y = uint(texel.y);
    const bool oddColumn = (x & 1) != 0;
    const uint swap = (y & 1) != 0 ? 4 : 0;
    const bool split = tile.format == FormatRgba && tile.size == 3;
    const bool palette = tlut != TlutNone;
    const uint shift = split ? 2 : tile.size;
    const uint mask = (split || palette) ? 0x7FF : 0xFFF;
    const uint base = tile.address << 3;
    const uint address = (y * (tile.rowWords << 3)) + ((x << shift) >> 1);

    const uint first = loadByte((base + (address ^ swap)) & mask);
    const uint nibble = (first >> (oddColumn ? 0 : 4)) & 0xF;
    if (palette)
    {
        const uint entry = tile.size == 0 ? 0x800 + (tile.palette << 7) + (nibble << 3)
                                          : 0x800 + (first << 3);
        const uint value = (loadByte(entry) << 8) | loadByte(entry + 1);
        return tlut == TlutIntensityAlpha16 ? decodeIntensityAlpha16(value) : decodeRgba16(value);
    }

    const uint second = loadByte((base + ((address + 1) ^ swap)) & mask);
    if (tile.size == 0)
    {
        if (tile.format == FormatColorIndex)
        {
            return makeGray((tile.palette << 4) | nibble);
        }
        if (tile.format == FormatIntensityAlpha)
        {
            const uint level = nibble >> 1;
            const int intensity = int((level << 5) | (level << 2) | (level >> 1));
            return int4(intensity, intensity, intensity, (nibble & 1) != 0 ? 0xFF : 0);
        }
        if (tile.format == FormatIntensity || tile.format == FormatRgba)
        {
            return makeGray(nibble * 0x11);
        }
        return int4(0, 0, 0, 0xFF);
    }
    if (tile.size == 1)
    {
        if (tile.format == FormatIntensityAlpha)
        {
            const int intensity = int((first >> 4) * 0x11);
            return int4(intensity, intensity, intensity, int((first & 0xF) * 0x11));
        }
        if (tile.format == FormatIntensity || tile.format == FormatRgba ||
            tile.format == FormatColorIndex)
        {
            return makeGray(first);
        }
        return int4(0, 0, 0, 0xFF);
    }
    if (tile.size == 2)
    {
        if (tile.format == FormatRgba)
        {
            return decodeRgba16((first << 8) | second);
        }
        if (tile.format == FormatIntensityAlpha)
        {
            return decodeIntensityAlpha16((first << 8) | second);
        }
        if (tile.format == FormatColorIndex || tile.format == FormatIntensity)
        {
            return int4(first, second, first, second);
        }
        return int4(0, 0, 0, 0xFF);
    }
    const uint upperAddress = split ? address : address + 2;
    const uint upper = split ? 0x800 : 0;
    const uint third = loadByte(((base + (upperAddress ^ swap)) & mask) | upper);
    const uint fourth = loadByte(((base + ((upperAddress + 1) ^ swap)) & mask) | upper);
    if (tile.format == FormatRgba)
    {
        return int4(first, second, third, fourth);
    }
    if (tile.format == FormatColorIndex || tile.format == FormatIntensityAlpha ||
        tile.format == FormatIntensity)
    {
        return oddColumn ? int4(first, second, first, second) : int4(third, fourth, third, fourth);
    }
    return int4(0, 0, 0, 0xFF);
}

int4 sampleTile(Tile tile, float2 texels, uint tlut, uint filter)
{
    const int2 coordinate = findTileCoordinate(tile, texels);
    const int2 base = coordinate >> 5;
    const int2 fraction = coordinate & 31;
    const int4 t00 = fetchTexel(tile, wrapTexel(tile, base), tlut);
    if (filter == FilterPoint)
    {
        return t00;
    }
    const int4 t10 = fetchTexel(tile, wrapTexel(tile, base + int2(1, 0)), tlut);
    const int4 t01 = fetchTexel(tile, wrapTexel(tile, base + int2(0, 1)), tlut);
    const int4 t11 = fetchTexel(tile, wrapTexel(tile, base + int2(1, 1)), tlut);
    if (filter == FilterAverage && fraction.x == 16 && fraction.y == 16)
    {
        return (t00 + t10 + t01 + t11 + 2) >> 2;
    }
    const bool upper = fraction.x + fraction.y >= 32;
    const int4 origin = upper ? t11 : t00;
    const int2 weights = upper ? (32 - fraction.yx) : fraction;
    return origin + ((((t10 - origin) * weights.x) + ((t01 - origin) * weights.y) + 0x10) >> 5);
}

static const uint InputCombined = 0;
static const uint InputTexel0 = 1;
static const uint InputTexel1 = 2;
static const uint InputPrimitive = 3;
static const uint InputShade = 4;
static const uint InputEnvironment = 5;
static const uint InputOne = 6;
static const uint InputZero = 7;
static const uint InputNoise = 8;
static const uint InputKeyCenter = 9;
static const uint InputKeyScale = 10;
static const uint InputConvertK4 = 11;
static const uint InputConvertK5 = 12;
static const uint InputCombinedAlpha = 13;
static const uint InputTexel0Alpha = 14;
static const uint InputTexel1Alpha = 15;
static const uint InputPrimitiveAlpha = 16;
static const uint InputShadeAlpha = 17;
static const uint InputEnvironmentAlpha = 18;
static const uint InputLodFraction = 19;
static const uint InputPrimitiveLodFraction = 20;

struct CombinerSources
{
    int4 combined;
    int4 texel0;
    int4 texel1;
    int4 shade;
    int noise;
};

int selectInput(uint input, uint channel, CombinerSources sources)
{
    const uint rgb = min(channel, 2u);
    switch (input)
    {
    case InputCombined: return sources.combined[channel];
    case InputTexel0: return sources.texel0[channel];
    case InputTexel1: return sources.texel1[channel];
    case InputPrimitive: return unpackColor(colors.x)[channel];
    case InputShade: return sources.shade[channel];
    case InputEnvironment: return unpackColor(colors.y)[channel];
    case InputOne: return 0x100;
    case InputNoise: return sources.noise;
    case InputKeyCenter: return int((constants.y >> (rgb * 8)) & 0xFF);
    case InputKeyScale: return int((constants.z >> (rgb * 8)) & 0xFF);
    case InputConvertK4: return int(constants.w & 0x1FF);
    case InputConvertK5: return int((constants.w >> 16) & 0x1FF);
    case InputCombinedAlpha: return sources.combined.a;
    case InputTexel0Alpha: return sources.texel0.a;
    case InputTexel1Alpha: return sources.texel1.a;
    case InputPrimitiveAlpha: return unpackColor(colors.x).a;
    case InputShadeAlpha: return sources.shade.a;
    case InputEnvironmentAlpha: return unpackColor(colors.y).a;
    case InputLodFraction: return 0;
    case InputPrimitiveLodFraction: return int(constants.x & 0xFF);
    default: return 0;
    }
}

int signExtendNine(int value)
{
    return ((value & 0x1FF) ^ 0x100) - 0x100;
}

int expandSpecial(int value)
{
    return signExtendNine(value - 0x80) + 0x80;
}

int clampNine(int value)
{
    return clamp(signExtendNine(value - 0x80) + 0x80, 0, 0xFF);
}

int4 runCycle(uint colorSlots, uint alphaSlots, CombinerSources sources)
{
    int4 result;
    [unroll]
    for (uint channel = 0; channel < 4; ++channel)
    {
        const uint slots = channel == 3 ? alphaSlots : colorSlots;
        const int a = expandSpecial(selectInput(slots & 0xFF, channel, sources));
        const int b = expandSpecial(selectInput((slots >> 8) & 0xFF, channel, sources));
        const int c = signExtendNine(selectInput((slots >> 16) & 0xFF, channel, sources));
        const int d = expandSpecial(selectInput(slots >> 24, channel, sources));
        result[channel] = ((((a - b) * c) + 0x80) >> 8) + d;
    }
    return result;
}

int expandAlpha(int alpha)
{
    return alpha + ((alpha + 1) >> 8);
}

int modulateByCoverage(int alpha, bool timesAlpha)
{
    return timesAlpha ? ((alpha * 8) + 4) >> 3 : 8 << 5;
}

struct CombinerOutput
{
    int4 color;
    int alphaReference;
};

CombinerOutput combine(CombinerSources sources)
{
    const uint flags = modes.x;
    const bool twoCycle = (flags & FlagTwoCycle) != 0;
    const bool coverageSelect = (flags & FlagAlphaCoverageSelect) != 0;
    const bool timesAlpha = (flags & FlagCoverageTimesAlpha) != 0;
    CombinerOutput output;
    output.alphaReference = 0;
    if (twoCycle)
    {
        sources.combined = runCycle(combiner.x, combiner.y, sources);
        int reference = expandAlpha(clampNine(sources.combined.a));
        if (coverageSelect)
        {
            reference = modulateByCoverage(reference, timesAlpha);
        }
        output.alphaReference = clamp(reference, 0, 0xFF);
        const int4 swapped = sources.texel0;
        sources.texel0 = sources.texel1;
        sources.texel1 = swapped;
    }
    const int4 combined = runCycle(combiner.z, combiner.w, sources);
    output.color.r = clampNine(combined.r);
    output.color.g = clampNine(combined.g);
    output.color.b = clampNine(combined.b);
    int alpha = expandAlpha(clampNine(combined.a));
    if (coverageSelect)
    {
        alpha = modulateByCoverage(alpha, timesAlpha);
    }
    output.color.a = clamp(alpha, 0, 0xFF);
    if (!twoCycle)
    {
        output.alphaReference = output.color.a;
    }
    return output;
}

static const uint BlendPixel = 0;
static const uint BlendMemory = 1;
static const uint BlendBlendColor = 2;
static const uint BlendFog = 3;

static const uint PathShader = 0;
static const uint PathOverMemory = 1;
static const uint PathMemoryUnder = 2;
static const uint PathKeepMemory = 3;

int3 pickBlendColor(uint input, int4 pixel)
{
    switch (input)
    {
    case BlendPixel: return pixel.rgb;
    case BlendBlendColor: return unpackColor(colors.z).rgb;
    case BlendFog: return unpackColor(colors.w).rgb;
    default: return int3(0, 0, 0);
    }
}

int pickFirstWeight(uint input, int4 pixel, int shadeAlpha)
{
    switch (input)
    {
    case 0: return pixel.a;
    case 1: return unpackColor(colors.w).a;
    case 2: return shadeAlpha;
    default: return 0;
    }
}

int pickSecondWeight(uint input, int firstWeight)
{
    switch (input)
    {
    case 0: return ~firstWeight & 0xFF;
    case 2: return 0xFF;
    default: return 0;
    }
}

int3 blendCycle(uint cycle, int4 pixel, int shadeAlpha, bool finalCycle)
{
    const uint first = (cycle >> 6) & 3;
    const uint firstWeightInput = (cycle >> 4) & 3;
    const uint second = (cycle >> 2) & 3;
    const uint secondWeightInput = cycle & 3;
    const int3 firstColor = pickBlendColor(first, pixel);
    const bool forceBlend = (modes.x & FlagForceBlend) != 0;
    const bool opaque = firstWeightInput == 0 && secondWeightInput == 0 && pixel.a == 0xFF;
    if (finalCycle && (!forceBlend || opaque))
    {
        return firstColor;
    }
    const int rawFirstWeight = pickFirstWeight(firstWeightInput, pixel, shadeAlpha);
    const int firstWeight = rawFirstWeight >> 3;
    const int secondWeight = pickSecondWeight(secondWeightInput, rawFirstWeight) >> 3;
    const int3 blended = (firstColor * firstWeight) + (pickBlendColor(second, pixel) * (secondWeight + 1));
    return (blended >> 5) & 0xFF;
}

uint hashPixel(uint2 pixel)
{
    uint h = (pixel.x * 0x8DA6B343u) ^ (pixel.y * 0xD8163841u);
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    return h ^ (h >> 15);
}

float4 FragmentMain(Interpolants input) : SV_Target0
{
    const uint flags = modes.x;
    if ((flags & FlagFill) != 0)
    {
        const int4 fill = (flags & FlagColorImage32) != 0 ? unpackColor(modes.z)
                                                          : decodeRgba16(modes.z >> 16);
        return float4(fill) / 255.0;
    }

    const Tile first = unpackTile(tile0);
    const Tile second = unpackTile(tile1);
    const uint tlut = (flags >> 12) & 0x3;
    const uint filter = (flags >> 14) & 0x3;
    const float2 texels = (flags & FlagPerspective) != 0 ? input.texture : input.texture * 0.5;
    const uint random = hashPixel(uint2(input.position.xy));

    CombinerSources sources;
    sources.combined = int4(0, 0, 0, 0);
    sources.texel0 = int4(0, 0, 0, 0);
    sources.texel1 = int4(0, 0, 0, 0);
    if ((flags & FlagTextured) != 0)
    {
        sources.texel0 = sampleTile(first, texels, tlut, filter);
        sources.texel1 = sampleTile(second, texels, tlut, filter);
    }
    sources.shade = (flags & FlagShade) != 0 ? int4(round(saturate(input.color) * 255.0)) : int4(0, 0, 0, 0);
    sources.noise = int(((random & 7) << 6) | 0x20);

    if ((flags & FlagCopy) != 0)
    {
        if ((flags & FlagAlphaCompare) != 0 && (flags & FlagColorImage32) == 0 &&
            sources.texel0.a == 0)
        {
            discard;
        }
        return float4(sources.texel0) / 255.0;
    }

    const CombinerOutput combined = combine(sources);
    if ((flags & FlagAlphaCompare) != 0)
    {
        const int threshold = (flags & FlagAlphaCompareDither) != 0 ? int((random >> 8) & 0xFF)
                                                                    : unpackColor(colors.z).a;
        if (combined.alphaReference < threshold)
        {
            discard;
        }
    }

    int4 pixel = combined.color;
    const uint blending = modes.w;
    const uint finalCycle = (blending >> 8) & 0xFF;
    const uint path = (blending >> 16) & 0x3;
    if ((flags & FlagTwoCycle) != 0)
    {
        pixel.rgb = blendCycle(blending & 0xFF, pixel, sources.shade.a, false);
    }
    if (path == PathShader)
    {
        return float4(float3(blendCycle(finalCycle, pixel, sources.shade.a, true)), pixel.a) / 255.0;
    }
    if (path == PathOverMemory)
    {
        const int weight = pickFirstWeight((finalCycle >> 4) & 3, pixel, sources.shade.a);
        return float4(float3(pickBlendColor((finalCycle >> 6) & 3, pixel)), weight) / 255.0;
    }
    if (path == PathMemoryUnder)
    {
        const int weight = pickFirstWeight((finalCycle >> 4) & 3, pixel, sources.shade.a);
        return float4(float3(pickBlendColor((finalCycle >> 2) & 3, pixel)), 0xFF - weight) / 255.0;
    }
    return float4(0.0, 0.0, 0.0, 0.0);
}

#endif
