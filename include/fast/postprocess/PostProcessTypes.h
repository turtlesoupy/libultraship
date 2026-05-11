// Implemented from the libretro/Mednafen public shader-uniform conventions
// (Source/TextureSize/OutputSize/FrameCount) and the plan in
// docs/crt_shader_plan_2026-05-11.md §3.2. No code copied from RetroArch
// or any GPL-licensed shader runtime.
#pragma once

#include <stdint.h>
#include <string>

namespace Fast {

// Per-frame inputs the runtime supplies to each post-process pass.
//
// Backends are responsible for converting these into whatever uniform
// storage the underlying API wants (a uniform block, push constants,
// loose glUniform calls, etc.). The set is intentionally minimal in
// Phase 1 and matches the "common five" uniforms found in essentially
// every single-file CRT/scanline shader.
struct PostProcessParams {
    uint32_t srcWidth;          // Game framebuffer dimensions (the
    uint32_t srcHeight;         // texture the pass samples from).
    uint32_t dstWidth;          // Output framebuffer dimensions (the
    uint32_t dstHeight;         // surface the pass renders into).
    uint32_t frameCount;        // Monotonic frame counter, wraps.
    float    frameDeltaSeconds; // Wall-clock delta since previous frame.
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
