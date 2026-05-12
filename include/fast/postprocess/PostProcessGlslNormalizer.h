// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3 / §4
// and the libretro single-file GLSL conventions documented at
// https://github.com/libretro/glsl-shaders/blob/master/README.md. No code
// copied from RetroArch or any GPL-licensed shader runtime.
#pragma once

#include <string>
#include <vector>

namespace Fast {

// One libretro `#pragma parameter` declaration parsed from a shader.
// Spec form: `#pragma parameter <name> "<label>" <default> <min> <max> [<step>]`.
// `name` is the uniform identifier the shader reads (the normalizer
// emits `uniform float <name>;` in the preamble and the transpiled UBO
// reserves a float slot for it). `label` is the human-readable string
// the shader-picker UI shows next to the slider. The numeric fields
// match the spec exactly; `step` defaults to (max-min)/100 when the
// pragma omits it.
struct PostProcessShaderParameter {
    std::string name;
    std::string label;
    float       defaultValue = 0.0f;
    float       minValue     = 0.0f;
    float       maxValue     = 1.0f;
    float       step         = 0.01f;
};

// Parse `#pragma parameter` declarations from a user-supplied GLSL
// source and return them in declaration order. Malformed pragmas are
// silently skipped — the spec doesn't define error recovery, and the
// alternative (failing the whole shader load on a typo) is worse for
// the picker UX. The normalizer separately strips the pragma lines so
// the GLSL compiler doesn't see them; this function operates on the
// raw user source (i.e. call it BEFORE NormalizeUserGlsl).
std::vector<PostProcessShaderParameter> ParseShaderParameters(const std::string& src);

// Normalize a user-supplied post-process GLSL source into the canonical
// LUS Phase-1 form so it loads regardless of which convention the shader
// was authored against:
//
//   1. LUS schema (matches libultraship/src/fast/shaders/postprocess/
//      scanlines.glsl) — passes through with only the `#version` line
//      reset.
//   2. libretro single-file (`#if defined(VERTEX) / #elif defined(FRAGMENT)`
//      combined VS+FS, `Texture` / `TextureSize` / `TEX0` / `FragColor`,
//      GLSL 120 with `varying` and `gl_FragColor`) — the VERTEX block is
//      skipped via `#define FRAGMENT`, identifiers are renamed to our
//      schema, and the shader compiles at `#version 330 core`.
//
// Output guarantees (consumed by both the OpenGL backend directly and
// the SPIRV-Cross transpile path):
//   - First non-empty line is `#version 330 core`
//   - Declares: `uniform sampler2D Source;`, `uniform vec2 SourceSize;`,
//     `uniform vec2 OutputSize;`, `uniform vec2 InputSize;`,
//     `uniform int FrameCount;`, `uniform float FrameDirection;`,
//     `in vec2 vTexCoord;`, `out vec4 fragColor;`
//   - User declarations of those identifiers (under any of the recognized
//     libretro aliases) are stripped before being re-introduced under our
//     names.
//
// The normalizer does not parse GLSL syntactically — it walks lines and
// rewrites identifier tokens by whole-word match. Shader authors who
// happen to use one of the schema/alias names as a local variable will
// see a duplicate-declaration error at GLSL compile time; renaming the
// local is the workaround.
std::string NormalizeUserGlsl(const std::string& src);

// Overload accepting an ordered alias list. Each alias `<name>` is
// declared in the preamble as `uniform sampler2D <name>` and
// `uniform vec2 <name>Size`, matching libretro `.glslp` alias
// conventions. User declarations of those names are stripped before
// re-injection (same treatment as Source / Original). `aliasNames`
// can be empty — the function then behaves identically to the single-
// argument overload.
std::string NormalizeUserGlsl(const std::string& src,
                              const std::vector<std::string>& aliasNames);

// Overload that accepts the parsed `#pragma parameter` list and
// declares each parameter as `uniform float <name>;` in the preamble.
// The list typically comes from a prior ParseShaderParameters call on
// the same source. Pass an empty vector to behave identically to the
// two-argument overload.
std::string NormalizeUserGlsl(const std::string& src,
                              const std::vector<std::string>& aliasNames,
                              const std::vector<PostProcessShaderParameter>& parameters);

} // namespace Fast
