#version 330 core

// MIT-licensed Lottes-style CRT shader for the LUS post-process runtime.
// Original work for this repository, implementing the classic Lottes CRT
// technique (subpixel triad mask + per-row scanline falloff + optional
// barrel warp + gamma in/out) from the published public description; no
// code copied from RetroArch / libretro/glsl-shaders or any GPL source.
//
// Uniform schema matches PostProcessTypes.h.

in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D Source;
uniform vec2 SourceSize;
uniform vec2 OutputSize;
uniform vec2 InputSize;
uniform int FrameCount;

// Curvature strength in normalized-coordinate units. 0.0 disables warp.
// Larger values bulge the image outward more strongly at the edges.
const vec2 kWarp = vec2(0.031, 0.041);

// Phosphor mask strength: 1.0 = full subpixel separation (very dark
// off-channels), 0.0 = mask disabled. Lottes original uses ~0.3 on PC,
// higher on phone-DPI displays.
const float kMaskStrength = 0.35;

// Scanline darkness profile. Lower values make scan gaps darker.
const float kScanMin = 0.55;

// Working in linear light; sRGB-decode on input and re-encode on output.
// Faster than per-pixel branchy pow() — approximate gamma 2.2 with pow.
vec3 SrgbToLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 LinearToSrgb(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

// Barrel distortion. Identity at uv = (0.5, 0.5); UVs near the edges get
// pushed outward by a quadratic in distance-from-center.
vec2 WarpUV(vec2 uv) {
    vec2 c = uv * 2.0 - 1.0;
    c *= 1.0 + (c.yx * c.yx) * kWarp;
    return c * 0.5 + 0.5;
}

// Three-tap horizontal filter approximating Lottes' per-source-pixel
// gather, weighting the nearest source column more heavily than its
// neighbors. SourceSize.x converts UV → source columns; the integer
// part picks the texel, fractional part drives the weight.
vec3 FetchScanline(vec2 uv, float row) {
    // Snap UV to the row center so the same scanline is sampled
    // regardless of where in the row the fragment lies.
    vec2 rowUv = vec2(uv.x, (row + 0.5) / SourceSize.y);
    vec2 dx = vec2(1.0 / SourceSize.x, 0.0);
    vec3 a = SrgbToLinear(texture(Source, rowUv - dx).rgb);
    vec3 b = SrgbToLinear(texture(Source, rowUv).rgb);
    vec3 c = SrgbToLinear(texture(Source, rowUv + dx).rgb);
    return (a * 0.25) + (b * 0.5) + (c * 0.25);
}

// Per-row scanline weight. `frac` is the fractional distance from the
// row center; the cosine gives a smooth dark-band peak at frac = 0.5.
float ScanWeight(float frac) {
    return mix(1.0, kScanMin, 0.5 - 0.5 * cos(frac * 6.283185));
}

// Output-pixel-aligned RGB triad mask. Three columns of physical pixels
// each emphasize one channel; the off-channels are dimmed by maskStrength.
vec3 PhosphorMask(vec2 fragPos) {
    float col = mod(fragPos.x, 3.0);
    float lo = 1.0 - kMaskStrength;
    if (col < 1.0) {
        return vec3(1.0, lo, lo);
    } else if (col < 2.0) {
        return vec3(lo, 1.0, lo);
    } else {
        return vec3(lo, lo, 1.0);
    }
}

void main() {
    vec2 uv = WarpUV(vTexCoord);

    // Drop pixels that warp outside the [0,1] frame so the curve produces
    // a hard mask edge instead of a stretched repeat.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Two adjacent source rows; blend their scanline contributions by the
    // sub-row coordinate. Both rows are kept in linear space.
    float rowF = uv.y * SourceSize.y - 0.5;
    float row0 = floor(rowF);
    float frac = rowF - row0;
    vec3 lineA = FetchScanline(uv, row0) * ScanWeight(frac);
    vec3 lineB = FetchScanline(uv, row0 + 1.0) * ScanWeight(frac - 1.0);

    vec3 color = lineA + lineB;

    color *= PhosphorMask(gl_FragCoord.xy);

    fragColor = vec4(LinearToSrgb(color), 1.0);
}
