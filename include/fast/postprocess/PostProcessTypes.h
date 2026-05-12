// Implemented from the libretro/Mednafen public shader-uniform conventions
// (Source/TextureSize/OutputSize/FrameCount) and the plan in
// docs/crt_shader_plan_2026-05-11.md §3.2. No code copied from RetroArch
// or any GPL-licensed shader runtime.
#pragma once

#include <stdint.h>
#include <string>

#include "PostProcessPreset.h"

namespace Fast {

// Per-pass output framebuffer pixel format. libretro `.glslp` exposes
// two booleans (`srgb_framebufferN`, `float_framebufferN`); we collapse
// them into a single enum since they're mutually exclusive in practice
// (libretro spec: setting both is undefined; real shaders only set one).
//
// Default = the backend's regular 8-bit color format (RGBA8 / RGB8 /
//   BGRA8Unorm depending on backend). Used for the chain's final pass
//   (mDstFb) regardless of what the .glslp says, because the GUI
//   consumes mDstFb through ImGui::Image which expects a sampleable
//   LDR texture.
// Srgb = sRGB-encoded 8-bit. Hardware auto-converts sRGB->linear on
//   sample and linear->sRGB on write (when the destination encoding is
//   live — GL requires GL_FRAMEBUFFER_SRGB enabled, D3D11/Metal handle
//   it via the RTV/colorAttachment format).
// Float16 = half-float per channel. No encoding; HDR linear math.
enum class PostProcessFboFormat {
    Default,
    Srgb,
    Float16,
};

// Per-frame inputs the runtime supplies to each post-process pass.
//
// Backends are responsible for converting these into whatever uniform
// storage the underlying API wants (a uniform block, push constants,
// loose glUniform calls, etc.). The set is intentionally minimal in
// Phase 1 and matches the "common five" uniforms found in essentially
// every single-file CRT/scanline shader.
//
// Resolution-uniform conventions match libretro / Mednafen:
//   - source = the texture being sampled (FBO at internal render res)
//   - input  = the original video signal the source was generated from
//              (for N64 emulation: 320x240, the screen res the GBI
//              commands target)
//   - dst    = the framebuffer the pass writes into (window)
// CRT shaders that emit per-source-row scanlines rely on the
// `source / input` ratio to convert from texture coordinates into
// game-pixel coordinates; passing identical values for both collapses
// the scanline period to one output pixel and aliases against the LCD
// subpixel grid as a moire pattern.
struct PostProcessParams {
    uint32_t srcWidth;          // Source texture pixel dimensions (the
    uint32_t srcHeight;         //   current pass's input sampler 0).
    uint32_t inputWidth;        // Original video-signal dimensions —
    uint32_t inputHeight;       //   N64 native 320x240 for SSB64.
    uint32_t originalWidth;     // Game-FB pixel dimensions (the
    uint32_t originalHeight;    //   sampler 1 / "Original" texture). Same
                                //   as src{Width,Height} on pass 0; for
                                //   later passes Source is the prior pass
                                //   output, Original stays the game FB.
    uint32_t dstWidth;          // Output framebuffer dimensions (the
    uint32_t dstHeight;         //   surface the pass renders into).
    uint32_t frameCount;        // Monotonic frame counter, wraps.
    float    frameDeltaSeconds; // Wall-clock delta since previous frame.

    // Sampler state for the `Source` binding (slot 0). The chain
    // populates these from the producer pass's libretro filter_linear /
    // wrap_mode keys — i.e. pass i's Source sampler is set from
    // pass[i-1].config. Pass 0 reads from the game FB, which has no
    // producer; the chain leaves the defaults (linear, clamp-to-edge)
    // and that input is rendered into by the regular game pipeline at
    // its own filtering settings anyway.
    //
    // The `Original` binding (slot 1) is intentionally pinned to the
    // backend default (linear, clamp-to-edge) — libretro's `.glslp` has
    // no per-pass control over the original FB sampler.
    bool                srcFilterLinear = true;
    PostProcessWrapMode srcWrapMode     = PostProcessWrapMode::ClampToEdge;
};

// Per-backend source text for a single fragment-shader pass.
//
// PostProcessSourceLoader populates `glsl` from a `<name>.glsl` on disk
// (or in f3d.o2r), then either copies hand-tuned `<name>.hlsl` /
// `<name>.msl` siblings into the matching slots or hands off to
// PostProcessTranspiler::SynthesizeMissing to fill them from `glsl`.
// Backends consume the slot matching their target API and ignore the
// others.
struct PostProcessSource {
    std::string name; // Diagnostic label (typically the shader filename).
    std::string glsl; // GLSL 330 core fragment-shader source.
    std::string hlsl; // HLSL SM 5.0 fragment-shader source.
    std::string msl;  // MSL 2.2 fragment-shader source.
};

} // namespace Fast
