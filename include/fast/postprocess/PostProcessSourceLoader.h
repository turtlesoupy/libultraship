// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.4.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#pragma once

#include <string>
#include <vector>

#include "PostProcessTypes.h"

namespace Fast {

// Resolve a post-process shader by short name and populate `out` with
// the source text. Lookup order:
//   1. Filesystem: `./shaders/<name>.glsl` relative to the executable's
//      working directory. This is where users drop their own shaders.
//   2. Builtins: `shaders/postprocess/<name>.glsl` inside f3d.o2r.
//
// Returns true on success. On failure `out` is left untouched and the
// reason is logged via SPDLOG_ERROR.
bool LoadPostProcessShader(const std::string& name, PostProcessSource& out);

// Names of builtin (in-archive) shaders. Used by ports to populate a
// menu picker without having to know about the resource manager.
std::vector<std::string> ListBuiltinPostProcessShaders();

} // namespace Fast
