// Implemented from the public libretro `.glslp` preset format documented
// at https://docs.libretro.com/development/shaders/glsl-shaders/ and the
// example presets in libretro/glsl-shaders. No code copied from
// RetroArch or any GPL-licensed shader runtime.
#pragma once

#include <string>
#include <vector>

namespace Fast {

enum class PostProcessScaleType {
    Source,   // Next FBO size = previous-pass output size * scale.
    Viewport, // Next FBO size = window viewport size * scale.
    Absolute, // Next FBO size = scale, rounded to int pixels.
};

// libretro `wrap_modeN` values. Applied to the producer pass's output
// when a later pass samples it as `Source`. Same mode is used for both
// the S and T axes (libretro never splits them).
enum class PostProcessWrapMode {
    ClampToEdge,     // libretro: clamp_to_edge (default).
    ClampToBorder,   // libretro: clamp_to_border (transparent-black outside).
    Repeat,          // libretro: repeat.
    MirroredRepeat,  // libretro: mirrored_repeat.
};

struct PostProcessPresetPass {
    std::string shaderPath;          // Path as written in the .glslp,
                                     //   relative to the preset's baseDir.
    bool        filterLinear   = false; // GL_NEAREST default mirrors libretro.
    PostProcessWrapMode wrapMode = PostProcessWrapMode::ClampToEdge;
    PostProcessScaleType scaleTypeX = PostProcessScaleType::Source;
    PostProcessScaleType scaleTypeY = PostProcessScaleType::Source;
    float       scaleX = 1.0f;
    float       scaleY = 1.0f;
    bool        srgbFramebuffer  = false; // v1 ignores — RGBA8 throughout.
    bool        floatFramebuffer = false; // v1 ignores.
    bool        mipmapInput      = false; // v1 ignores.
    int         frameCountMod    = 0;     // 0 = no modulo (libretro default).
    std::string alias;                    // Cross-pass reference name (optional).
};

// One external texture declared via the `.glslp` `textures = "..."`
// list. Backends upload the loaded RGBA8 bytes into a static texture
// and bind it (alongside alias FBOs) at the sampler slot reserved by
// the transpiler for this name.
struct PostProcessPresetTexture {
    std::string         name;        // Uniform identifier in the shader.
    std::string         path;        // PNG path relative to baseDir.
    bool                filterLinear = true; // libretro default differs from
                                             // `filter_linearN` (which defaults
                                             // false). For external textures
                                             // libretro picks true unless the
                                             // `_linear` key explicitly says no.
    PostProcessWrapMode wrapMode = PostProcessWrapMode::ClampToEdge;
    bool                mipmap   = false;    // Reserved for future phase;
                                             // ignored by backends in v1.
};

struct PostProcessPreset {
    std::string baseDir;                       // Directory holding the .glslp.
    std::vector<PostProcessPresetPass> passes; // 1+ entries on success.
    std::vector<PostProcessPresetTexture> textures; // 0+ external texture decls.
};

// Parse a libretro `.glslp` preset from in-memory text. `baseDir` is the
// directory the file came from; it is stored on the output preset so
// the caller can resolve each pass's relative shaderPath when loading
// the referenced `.glsl` files.
//
// Returns true on success. On failure `errOut` carries a one-line
// reason and `out` is left in an unspecified state — callers should not
// use it on failure.
bool ParsePostProcessPreset(const std::string& src, const std::string& baseDir,
                            PostProcessPreset& out, std::string& errOut);

} // namespace Fast
