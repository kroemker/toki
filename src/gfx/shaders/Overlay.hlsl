// Draws filled triangles and lines over the frame, each line a quad pushed
// out sideways on screen from its two ends, each triangle as it is. Positions go through the viewport as the ubershader's do, so a
// line along a triangle's edge lands on the edge's pixels at the edge's depth.

struct Interpolants
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

#ifdef VERTEX_STAGE

cbuffer OverlayUniforms : register(b0, space1)
{
    float4 viewportScale;
    float4 viewportTranslate;
    // The target's width and height, the depth bias, and 1 in the pass that
    // draws what the frame hides, 0 in the one that draws what it does not.
    float4 screen;
};

struct VertexInput
{
    float4 position : TEXCOORD0;
    float4 other : TEXCOORD1;
    float2 extrusion : TEXCOORD2;
    float4 color : TEXCOORD3;
    float hiddenOpacity : TEXCOORD4;
};

static const float DepthRange = 1023.0;

float4 placeOnTarget(float4 p)
{
    const float w = p.w;
    const float x = (p.x * viewportScale.x) + (viewportTranslate.x * w);
    const float y = (-p.y * viewportScale.y) + (viewportTranslate.y * w);
    const float z = (p.z * viewportScale.z) + (viewportTranslate.z * w);
    return float4((x * 2.0 / screen.x) - w, w - (y * 2.0 / screen.y), z / DepthRange, w);
}

Interpolants VertexMain(VertexInput input)
{
    const float4 here = placeOnTarget(input.position);
    const float4 other = placeOnTarget(input.other);
    const float2 along = ((other.xy / other.w) - (here.xy / here.w)) * screen.xy;
    const float span = sqrt(dot(along, along));
    const float2 across = span > 0.0 ? float2(-along.y, along.x) / span : float2(0.0, 0.0);
    const float2 offset = across * input.extrusion.x * input.extrusion.y / screen.xy;

    Interpolants output;
    output.position = float4(here.xy + (offset * here.w), here.z - (screen.z * here.w), here.w);
    output.color = float4(input.color.rgb, input.color.a * lerp(1.0, input.hiddenOpacity, screen.w));
    return output;
}

#endif

#ifdef FRAGMENT_STAGE

float4 FragmentMain(Interpolants input) : SV_Target0
{
    return input.color;
}

#endif
