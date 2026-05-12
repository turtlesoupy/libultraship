// Implemented from the public libretro `.glslp` preset format documented
// at https://docs.libretro.com/development/shaders/glsl-shaders/ and the
// example presets in libretro/glsl-shaders. No code copied from
// RetroArch or any GPL-licensed shader runtime.

#include "fast/postprocess/PostProcessPreset.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace Fast {

namespace {

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return std::string();
    }
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool ParseBool(const std::string& v, bool fallback) {
    std::string t;
    t.reserve(v.size());
    for (char c : v) {
        t.push_back(std::tolower(static_cast<unsigned char>(c)));
    }
    if (t == "true" || t == "1" || t == "yes" || t == "on") {
        return true;
    }
    if (t == "false" || t == "0" || t == "no" || t == "off") {
        return false;
    }
    return fallback;
}

bool ParseScaleType(const std::string& v, PostProcessScaleType& out) {
    if (v == "source") {
        out = PostProcessScaleType::Source;
        return true;
    }
    if (v == "viewport") {
        out = PostProcessScaleType::Viewport;
        return true;
    }
    if (v == "absolute") {
        out = PostProcessScaleType::Absolute;
        return true;
    }
    return false;
}

// Split "shader42" -> ("shader", 42). Returns false if the key has no
// numeric suffix (= it's a global key like "shaders" or "textures").
bool SplitIndexed(const std::string& key, std::string& baseOut, int& indexOut) {
    if (key.empty() || !std::isdigit(static_cast<unsigned char>(key.back()))) {
        return false;
    }
    size_t splitPos = key.size();
    while (splitPos > 0 && std::isdigit(static_cast<unsigned char>(key[splitPos - 1]))) {
        splitPos -= 1;
    }
    if (splitPos == 0) {
        return false;
    }
    baseOut = key.substr(0, splitPos);
    indexOut = std::atoi(key.c_str() + splitPos);
    return true;
}

void EnsureCapacity(std::vector<PostProcessPresetPass>& passes, int index) {
    if (index < 0) {
        return;
    }
    while (static_cast<int>(passes.size()) <= index) {
        passes.emplace_back();
    }
}

} // namespace

bool ParsePostProcessPreset(const std::string& src, const std::string& baseDir,
                            PostProcessPreset& out, std::string& errOut) {
    out.baseDir = baseDir;
    out.passes.clear();

    int declaredShaders = -1;

    std::istringstream in(src);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo += 1;
        // Strip everything from the first `#` onward — libretro presets
        // use `#` for end-of-line comments. Quoted strings don't contain
        // `#` in practice, so we don't worry about it inside quotes.
        const size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            errOut = std::string(".glslp line ") + std::to_string(lineNo) +
                     ": missing '=' (\"" + line + "\")";
            return false;
        }
        const std::string key = Trim(line.substr(0, eq));
        const std::string val = Unquote(Trim(line.substr(eq + 1)));

        if (key == "shaders") {
            declaredShaders = std::atoi(val.c_str());
            if (declaredShaders < 0) {
                declaredShaders = 0;
            }
            EnsureCapacity(out.passes, declaredShaders - 1);
            continue;
        }
        if (key == "textures") {
            // v1: external lookup textures are unsupported. Quietly
            // ignore the list so presets that declare them still load
            // their pass chain — the shaders that reference the texture
            // by uniform name will get a fallback (transpiler emits an
            // undeclared sampler error which surfaces in logs).
            continue;
        }

        std::string base;
        int index = -1;
        if (!SplitIndexed(key, base, index)) {
            // Unknown global key — preset-parameter overrides etc. land
            // here. Silently skip so future / unsupported properties
            // don't fail the parse.
            continue;
        }
        EnsureCapacity(out.passes, index);
        PostProcessPresetPass& pass = out.passes[index];

        if (base == "shader") {
            pass.shaderPath = val;
        } else if (base == "filter_linear") {
            pass.filterLinear = ParseBool(val, pass.filterLinear);
        } else if (base == "srgb_framebuffer") {
            pass.srgbFramebuffer = ParseBool(val, pass.srgbFramebuffer);
        } else if (base == "float_framebuffer") {
            pass.floatFramebuffer = ParseBool(val, pass.floatFramebuffer);
        } else if (base == "mipmap_input") {
            pass.mipmapInput = ParseBool(val, pass.mipmapInput);
        } else if (base == "frame_count_mod") {
            pass.frameCountMod = std::atoi(val.c_str());
        } else if (base == "alias") {
            pass.alias = val;
        } else if (base == "scale_type") {
            // Sets both axes. Libretro spec: applies after any
            // axis-specific overrides earlier in the file, so do it
            // first and let scale_type_{x,y} override later.
            PostProcessScaleType st;
            if (ParseScaleType(val, st)) {
                pass.scaleTypeX = st;
                pass.scaleTypeY = st;
            }
        } else if (base == "scale_type_x") {
            ParseScaleType(val, pass.scaleTypeX);
        } else if (base == "scale_type_y") {
            ParseScaleType(val, pass.scaleTypeY);
        } else if (base == "scale") {
            const float s = static_cast<float>(std::atof(val.c_str()));
            pass.scaleX = s;
            pass.scaleY = s;
        } else if (base == "scale_x") {
            pass.scaleX = static_cast<float>(std::atof(val.c_str()));
        } else if (base == "scale_y") {
            pass.scaleY = static_cast<float>(std::atof(val.c_str()));
        }
        // Anything else (`wrap_mode<N>`, `parameters`, etc.) silently
        // ignored — v1 doesn't bind wrap modes or expose preset
        // parameters yet. Adding them later doesn't reshape this
        // function.
    }

    if (out.passes.empty()) {
        errOut = ".glslp declared no passes";
        return false;
    }

    // Trim trailing default-initialized passes that weren't assigned a
    // shader path (some presets declare shaders=N but only fill 0..M-1
    // and we'd otherwise carry phantom passes).
    while (!out.passes.empty() && out.passes.back().shaderPath.empty()) {
        out.passes.pop_back();
    }
    if (out.passes.empty()) {
        errOut = ".glslp had no pass with a shader path";
        return false;
    }
    for (size_t i = 0; i < out.passes.size(); ++i) {
        if (out.passes[i].shaderPath.empty()) {
            errOut = std::string(".glslp pass ") + std::to_string(i) +
                     " missing 'shader" + std::to_string(i) + "' entry";
            return false;
        }
    }
    return true;
}

} // namespace Fast
