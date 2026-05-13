// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.4.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#pragma once

#include <string>
#include <vector>

#include "PostProcessPreset.h"
#include "PostProcessSlangTranspiler.h"
#include "PostProcessTypes.h"

namespace Fast {

// One PNG external texture decoded into a RGBA8 pixel buffer, ready
// for the backend's CreatePostProcessStaticTexture call. The `name`
// matches the `.glslp` `textures = "..."` declaration and the shader
// uniform of the same name. `rgba8` is row-major, top-down,
// width*height*4 bytes.
struct PostProcessShaderExternalTexture {
    std::string         name;
    uint32_t            width  = 0;
    uint32_t            height = 0;
    std::vector<uint8_t> rgba8;
    bool                filterLinear = true;
    PostProcessWrapMode wrapMode = PostProcessWrapMode::ClampToEdge;
};

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
    // External `textures = "..."` declarations resolved to RGBA8.
    // PostProcessChain::LoadPasses uploads each via the rendering
    // API's CreatePostProcessStaticTexture and binds them per-pass.
    // Empty for single-pass shaders and presets without any
    // `textures =` declaration.
    std::vector<PostProcessShaderExternalTexture> externalTextures;
};

// Phase 3F: bundle holding the per-pass slang artifacts ready to
// hand to PostProcessChain::LoadSlangPasses. Distinct from
// PostProcessShaderBundle because the chain Run path branches on
// which one is loaded — slang uses authored VS, UBO uniforms, and
// libretro semantic samplers, while the legacy path uses a stub
// VS plus loose-uniform LUS schema.
struct PostProcessSlangShaderBundle {
    // Diagnostic name (the cvar value the user picked).
    std::string name;
    std::vector<PostProcessSlangArtifact> artifacts;
    std::vector<PostProcessPresetPass>    configs;
    // Per-pass display names for logging (e.g. "myshader[0/blur]").
    std::vector<std::string> diagnosticNames;
};

// Resolve a post-process shader by short name and populate `out`.
// Lookup order:
//   1. Filesystem: `<user-data>/shaders/<name>.{glslp,glsl}`, where
//      `<user-data>` is the LUS per-user app-data directory
//      (Ship::Context::GetPathRelativeToAppDirectory). On NON_PORTABLE
//      builds this is the OS app-data location (macOS
//      ~/Library/Application Support/<app>/shaders, Linux
//      $XDG_DATA_HOME/<app>/shaders, Windows %APPDATA%\<app>\shaders);
//      on portable builds it collapses to `./shaders` (same as 2).
//   2. Filesystem: `./shaders/<name>.{glslp,glsl}` relative to the
//      executable's working directory — kept as a development fallback
//      so contributors iterating from a source checkout do not need
//      to copy every shader into the per-user data dir each rebuild.
//   3. Builtins: `shaders/postprocess/<name>.{glslp,glsl}` inside
//      f3d.o2r.
//
// For `.glslp` presets, each per-pass shader path is resolved
// relative to the directory the `.glslp` itself came from (or, in
// the archive case, the archive subdir holding the `.glslp`). The
// candidate filesystem roots above are tried in order for each pass
// shader, so a preset in the per-user dir whose passes live next to
// it works without extra plumbing.
//
// Returns true on success. On failure `out` is left in an unspecified
// state and the reason is logged via SPDLOG_ERROR.
bool LoadPostProcessShader(const std::string& name, PostProcessShaderBundle& out);

// Phase 3F: resolve a slang post-process shader by short name. Same
// candidate-roots / archive-fallback lookup as LoadPostProcessShader,
// but probes `.slangp` first (multi-pass slang preset) and falls
// back to `.slang` (single-pass authored shader). Returns true on
// success; on failure `out` is left in an unspecified state and the
// reason is logged.
//
// Implementation requires LUS_ENABLE_POSTPROCESS_TRANSPILER. With
// the transpiler disabled at build time, the slang -> SPIR-V ->
// backend pipeline can't run, and this function returns false with
// a one-time log so the chain doesn't retry every frame.
bool LoadPostProcessSlangShader(const std::string& name, PostProcessSlangShaderBundle& out);

// Names of builtin (in-archive) shaders. Used by ports to populate a
// menu picker without having to know about the resource manager.
std::vector<std::string> ListBuiltinPostProcessShaders();

// One folder under the per-user shaders directory, listed by the
// picker. `displayName` is what the picker shows ("Bundled" for the
// builtin group, the relative folder path for user-installed shaders,
// "(loose)" for shaders living directly in the user data root).
// `shaderNames` are the short names (no extension) suitable for
// passing back to LoadPostProcessShader / the gPostProcessShader cvar.
// Entries are de-duplicated when the same short name has both a
// `.glslp` and a `.glsl` on disk (the .glslp wins, matching the
// loader's priority).
struct UserPostProcessShaderFolder {
    std::string displayName;
    std::vector<std::string> shaderNames;
};

// Walk the per-user shaders directory (creating it if absent) and
// return one entry per folder (plus a synthetic entry for any shaders
// living directly in the root). Returns an empty vector if neither
// the user-data dir nor the cwd-relative fallback exists yet — the
// caller may still surface ListBuiltinPostProcessShaders separately.
//
// This is intended for menu pickers. It does NOT load or compile the
// shaders, only discovers their presence.
std::vector<UserPostProcessShaderFolder> ListUserPostProcessShaders();

} // namespace Fast
