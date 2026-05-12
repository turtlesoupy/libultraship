// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.4
// and the libretro `.glslp` format documented at libretro/glsl-shaders.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#include "fast/postprocess/PostProcessSourceLoader.h"

#include <cstddef>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>

#include "fast/postprocess/PostProcessGlslNormalizer.h"
#include "fast/postprocess/PostProcessPreset.h"
#include "fast/postprocess/PostProcessTranspiler.h"
#include "ship/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/File.h"

namespace Fast {

namespace {

bool ReadFilesystemFile(const std::string& path, std::string& outText) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outText = ss.str();
    return true;
}

bool ReadArchiveFile(const std::string& path, std::string& outText) {
    auto ctx = Ship::Context::GetInstance();
    if (ctx == nullptr) {
        return false;
    }
    auto rm = ctx->GetResourceManager();
    if (rm == nullptr) {
        return false;
    }
    auto file = rm->LoadFileProcess(path);
    if (file == nullptr || !file->IsLoaded || file->Buffer == nullptr) {
        return false;
    }
    const auto& buf = *file->Buffer;
    if (file->BufferOffset >= buf.size()) {
        return false;
    }
    outText.assign(buf.begin() + file->BufferOffset, buf.end());
    return true;
}

// Read a file from filesystem `<fsBase>/<rel>` first, then archive
// `<arBase>/<rel>`. Returns true and populates outText on the first
// success.
bool ReadShaderFile(const std::string& fsBase, const std::string& arBase,
                    const std::string& rel, std::string& outText) {
    std::string fsPath = fsBase;
    if (!fsPath.empty() && fsPath.back() != '/') {
        fsPath += '/';
    }
    fsPath += rel;
    if (ReadFilesystemFile(fsPath, outText)) {
        return true;
    }
    std::string arPath = arBase;
    if (!arPath.empty() && arPath.back() != '/') {
        arPath += '/';
    }
    arPath += rel;
    return ReadArchiveFile(arPath, outText);
}

// Build the diagnostic display name for an individual pass. Strips
// any leading directory components and trailing extension so log
// messages stay readable for shaders like
// "shaders/crt-easymode-halation/blur_horiz.glsl".
std::string ShortenPassName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t begin = (slash == std::string::npos) ? 0 : slash + 1;
    const size_t dot = path.find_last_of('.');
    const size_t end = (dot != std::string::npos && dot > begin) ? dot : path.size();
    return path.substr(begin, end - begin);
}

// Run the user-shader pipeline on a raw GLSL string: normalize ->
// transpile to HLSL/MSL via SPIRV-Cross. Returns a fully-populated
// PostProcessSource ready for PostProcessChain::LoadPasses.
PostProcessSource MakeSource(const std::string& displayName, std::string rawGlsl) {
    PostProcessSource src;
    src.name = displayName;
    src.glsl = NormalizeUserGlsl(rawGlsl);
    std::string err;
    if (!PostProcessTranspiler::SynthesizeMissing(src, err)) {
        SPDLOG_WARN("Post-process shader '{}' could not be transpiled: {}", displayName, err);
    }
    return src;
}

bool LoadSinglePassBundle(const std::string& name, const std::string& fsBase,
                          const std::string& arBase, PostProcessShaderBundle& out) {
    std::string raw;
    if (!ReadShaderFile(fsBase, arBase, name + ".glsl", raw)) {
        return false;
    }
    out.name = name;
    out.sources.clear();
    out.configs.clear();
    out.sources.push_back(MakeSource(name, std::move(raw)));
    out.configs.emplace_back(); // Default scale_type=source, scale=1.0.
    return true;
}

// Parse a .glslp at `<fsBase>/<name>.glslp` or `<arBase>/<name>.glslp`,
// load each referenced pass shader through the normalize+transpile
// pipeline, and stuff everything into `out`. The pass shader paths in
// the preset are resolved relative to the preset's own location.
bool LoadPresetBundle(const std::string& name, const std::string& fsBase,
                      const std::string& arBase, PostProcessShaderBundle& out) {
    std::string presetText;
    if (!ReadShaderFile(fsBase, arBase, name + ".glslp", presetText)) {
        return false;
    }
    PostProcessPreset preset;
    std::string err;
    // baseDir is informational; the actual lookup happens against
    // fsBase / arBase below. We pass empty so the preset record
    // doesn't carry stale absolute paths.
    if (!ParsePostProcessPreset(presetText, std::string(), preset, err)) {
        SPDLOG_ERROR("Post-process preset '{}': {}", name, err);
        return false;
    }

    out.name = name;
    out.sources.clear();
    out.configs.clear();
    out.sources.reserve(preset.passes.size());
    out.configs.reserve(preset.passes.size());

    for (size_t i = 0; i < preset.passes.size(); ++i) {
        const PostProcessPresetPass& passCfg = preset.passes[i];
        std::string raw;
        if (!ReadShaderFile(fsBase, arBase, passCfg.shaderPath, raw)) {
            SPDLOG_ERROR("Post-process preset '{}' pass {}: shader '{}' not found "
                         "(searched filesystem '{}' and archive '{}')",
                         name, i, passCfg.shaderPath, fsBase, arBase);
            return false;
        }
        const std::string displayName =
            name + "[" + std::to_string(i) + "/" + ShortenPassName(passCfg.shaderPath) + "]";
        out.sources.push_back(MakeSource(displayName, std::move(raw)));
        out.configs.push_back(passCfg);
    }
    return true;
}

} // namespace

bool LoadPostProcessShader(const std::string& name, PostProcessShaderBundle& out) {
    if (name.empty()) {
        return false;
    }
    constexpr const char* kFsBase = "shaders";
    constexpr const char* kArBase = "shaders/postprocess";

    // Multi-pass presets take priority — a directory containing both a
    // `<name>.glslp` and a `<name>.glsl` is unusual (the .glsl would
    // typically be one of the passes), and if both exist the preset
    // is the more-explicit intent.
    if (LoadPresetBundle(name, kFsBase, kArBase, out)) {
        return true;
    }
    if (LoadSinglePassBundle(name, kFsBase, kArBase, out)) {
        return true;
    }
    SPDLOG_ERROR("Post-process shader '{}' not found "
                 "(tried '{}/{}.{{glslp,glsl}}' and archive '{}/{}.{{glslp,glsl}}')",
                 name, kFsBase, name, kArBase, name);
    return false;
}

std::vector<std::string> ListBuiltinPostProcessShaders() {
    // Mirrors what GenerateF3DO2R packages from
    // libultraship/src/fast/shaders/postprocess/. Hardcoded rather
    // than enumerated at runtime so the menu picker stays stable
    // across archive contents and so a port that ships a stripped
    // f3d.o2r still gets a usable default selection.
    return { "scanlines", "crt-lottes" };
}

} // namespace Fast
