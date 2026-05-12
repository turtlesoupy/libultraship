// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.4.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#pragma once

#include <string>
#include <vector>

#include "PostProcessPreset.h"
#include "PostProcessTypes.h"

namespace Fast {

// One-or-many fragment-shader passes plus their per-pass scale/filter
// metadata, ready to hand to PostProcessChain::LoadPasses. Single
// `.glsl` shaders produce a one-element bundle; `.glslp` presets
// produce one element per pass with the preset's scale_type/scale
// values baked into the configs vector.
struct PostProcessShaderBundle {
    // Diagnostic name (the cvar value the user picked).
    std::string name;
    std::vector<PostProcessSource> sources;
    std::vector<PostProcessPresetPass> configs;
};

// Resolve a post-process shader by short name and populate `out`.
// Lookup order:
//   1. Filesystem: `./shaders/<name>.glslp` then `<name>.glsl`,
//      relative to the executable's working directory. This is where
//      users drop their own shaders.
//   2. Builtins: `shaders/postprocess/<name>.{glslp,glsl}` inside
//      f3d.o2r.
//
// For `.glslp` presets, each per-pass shader path is resolved
// relative to the directory the `.glslp` itself came from (or, in
// the archive case, the archive subdir holding the `.glslp`).
//
// Returns true on success. On failure `out` is left in an unspecified
// state and the reason is logged via SPDLOG_ERROR.
bool LoadPostProcessShader(const std::string& name, PostProcessShaderBundle& out);

// Names of builtin (in-archive) shaders. Used by ports to populate a
// menu picker without having to know about the resource manager.
std::vector<std::string> ListBuiltinPostProcessShaders();

} // namespace Fast
