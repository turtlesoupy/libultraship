// MIT-licensed Lottes-style CRT shader (HLSL companion to crt-lottes.glsl).
// Hand-authored Phase 1 stand-in for the SPIRV-Cross transpiler that will
// eventually synthesize HLSL from the .glsl source. See the .glsl file
// for the technique reference and authorship note.

Texture2D Source : register(t0);
SamplerState SourceSampler : register(s0);

cbuffer PostProcessUniforms : register(b0) {
    float2 SourceSize;
    float2 OutputSize;
    float2 InputSize;
    int FrameCount;
    float FrameDirection;
};

struct VOut {
    float4 position : SV_Position;
    float2 vTexCoord : TEXCOORD0;
};

static const float2 kWarp = float2(0.031, 0.041);
static const float kMaskStrength = 0.35;
static const float kScanMin = 0.55;

float3 SrgbToLinear(float3 c) { return pow(c, float3(2.2, 2.2, 2.2)); }
float3 LinearToSrgb(float3 c) { return pow(c, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2)); }

float2 WarpUV(float2 uv) {
    float2 c = uv * 2.0 - 1.0;
    c *= 1.0 + (c.yx * c.yx) * kWarp;
    return c * 0.5 + 0.5;
}

float ScanWeight(float frac) {
    return lerp(1.0, kScanMin, 0.5 - 0.5 * cos(frac * 6.283185));
}

float3 FetchScanline(float2 uv, float row) {
    float2 rowUv = float2(uv.x, (row + 0.5) / SourceSize.y);
    float2 dx = float2(1.0 / SourceSize.x, 0.0);
    float3 a = SrgbToLinear(Source.Sample(SourceSampler, rowUv - dx).rgb);
    float3 b = SrgbToLinear(Source.Sample(SourceSampler, rowUv).rgb);
    float3 c = SrgbToLinear(Source.Sample(SourceSampler, rowUv + dx).rgb);
    return a * 0.25 + b * 0.5 + c * 0.25;
}

float3 PhosphorMask(float2 fragPos) {
    float col = fmod(fragPos.x, 3.0);
    float lo = 1.0 - kMaskStrength;
    if (col < 1.0) {
        return float3(1.0, lo, lo);
    } else if (col < 2.0) {
        return float3(lo, 1.0, lo);
    } else {
        return float3(lo, lo, 1.0);
    }
}

VOut VSMain(uint vid : SV_VertexID) {
    static const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };
    static const float2 texCoords[3] = {
        float2(0.0, 1.0),
        float2(2.0, 1.0),
        float2(0.0, -1.0),
    };
    VOut o;
    o.position = float4(positions[vid], 0.0, 1.0);
    o.vTexCoord = texCoords[vid];
    return o;
}

float4 PSMain(VOut input) : SV_Target {
    float2 uv = WarpUV(input.vTexCoord);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float rowF = uv.y * SourceSize.y - 0.5;
    float row0 = floor(rowF);
    float frac = rowF - row0;
    float3 lineA = FetchScanline(uv, row0) * ScanWeight(frac);
    float3 lineB = FetchScanline(uv, row0 + 1.0) * ScanWeight(frac - 1.0);

    float3 color = lineA + lineB;
    color *= PhosphorMask(input.position.xy);

    return float4(LinearToSrgb(color), 1.0);
}
