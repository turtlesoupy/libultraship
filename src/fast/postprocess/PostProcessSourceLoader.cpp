// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.4
// and the libretro `.glslp` format documented at libretro/glsl-shaders.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#include "fast/postprocess/PostProcessSourceLoader.h"

#include <cstddef>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>
#include <stb_image.h>

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

// Read a binary file (no text-mode line conversion) for stb_image
// consumption. Same filesystem-then-archive lookup as ReadShaderFile.
bool ReadShaderBinaryFile(const std::string& fsBase, const std::string& arBase,
                          const std::string& rel, std::vector<uint8_t>& outBytes) {
    std::string fsPath = fsBase;
    if (!fsPath.empty() && fsPath.back() != '/') {
        fsPath += '/';
    }
    fsPath += rel;
    std::ifstream in(fsPath, std::ios::binary);
    if (in.is_open()) {
        in.seekg(0, std::ios::end);
        const std::streamoff len = in.tellg();
        in.seekg(0, std::ios::beg);
        if (len > 0) {
            outBytes.resize(static_cast<size_t>(len));
            in.read(reinterpret_cast<char*>(outBytes.data()), len);
            if (in.good() || in.eof()) {
                return true;
            }
        }
    }
    std::string arPath = arBase;
    if (!arPath.empty() && arPath.back() != '/') {
        arPath += '/';
    }
    arPath += rel;
    auto ctx = Ship::Context::GetInstance();
    if (ctx == nullptr) {
        return false;
    }
    auto rm = ctx->GetResourceManager();
    if (rm == nullptr) {
        return false;
    }
    auto file = rm->LoadFileProcess(arPath);
    if (file == nullptr || !file->IsLoaded || file->Buffer == nullptr) {
        return false;
    }
    const auto& buf = *file->Buffer;
    if (file->BufferOffset >= buf.size()) {
        return false;
    }
    outBytes.assign(buf.begin() + file->BufferOffset, buf.end());
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

// Decode a PNG (or any stb_image-supported format) into an RGBA8 byte
// buffer. Returns true on success and populates `outTex.rgba8`,
// `outTex.width`, `outTex.height`; logs and returns false otherwise.
// `pngBytes` is the raw file contents (entire image, not streaming).
bool DecodeExternalTexture(const std::string& diagName,
                           const std::vector<uint8_t>& pngBytes,
                           PostProcessShaderExternalTexture& outTex) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(pngBytes.data(),
                                             static_cast<int>(pngBytes.size()),
                                             &w, &h, &channels,
                                             /*desired_channels=*/4);
    if (decoded == nullptr || w <= 0 || h <= 0) {
        SPDLOG_ERROR("Post-process external texture '{}': stbi_load_from_memory failed ({})",
                     diagName, stbi_failure_reason() ? stbi_failure_reason() : "no detail");
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        return false;
    }
    const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    outTex.width = static_cast<uint32_t>(w);
    outTex.height = static_cast<uint32_t>(h);
    outTex.rgba8.assign(decoded, decoded + byteCount);
    stbi_image_free(decoded);
    return true;
}

// Run the user-shader pipeline on a raw GLSL string: normalize ->
// transpile to HLSL/MSL via SPIRV-Cross. Returns a fully-populated
// PostProcessSource ready for PostProcessChain::LoadPasses.
//
// `aliasNames` carries the .glslp `aliasN` declarations from earlier
// passes (or this pass and later — the chain leaves slot bindings
// stable per preset). The normalizer and transpiler use them to
// declare matching `uniform sampler2D <name>` bindings in each
// language. Single-pass `.glsl` files pass an empty vector.
PostProcessSource MakeSource(const std::string& displayName, std::string rawGlsl,
                             const std::vector<std::string>& aliasNames) {
    PostProcessSource src;
    src.name = displayName;
    src.aliasNames = aliasNames;
    src.glsl = NormalizeUserGlsl(rawGlsl, aliasNames);
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
    out.sources.push_back(MakeSource(name, std::move(raw), {}));
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

    // Collect the preset's named-sampler bindings in declaration order:
    // first the per-pass `aliasN` outputs, then the external
    // `textures = "..."` entries. Each pass's compile sees the same
    // ordered list so the binding slots (2 + index) stay stable across
    // the chain — pass 0 binds slot 2 to the same name pass 4 does, and
    // external textures land at the same slot for every pass that
    // references them.
    std::vector<std::string> aliasNames;
    aliasNames.reserve(preset.passes.size() + preset.textures.size());
    for (const auto& passCfg : preset.passes) {
        if (passCfg.alias.empty()) {
            continue;
        }
        bool dup = false;
        for (const std::string& existing : aliasNames) {
            if (existing == passCfg.alias) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            aliasNames.push_back(passCfg.alias);
        }
    }
    for (const auto& tex : preset.textures) {
        if (tex.name.empty()) {
            continue;
        }
        bool dup = false;
        for (const std::string& existing : aliasNames) {
            if (existing == tex.name) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            aliasNames.push_back(tex.name);
        }
    }

    // Decode every external texture up front. Failure on any one
    // texture aborts the load — better to surface the error
    // immediately than render with a black slot the user will assume
    // is a shader bug. The texture path is resolved relative to the
    // .glslp's directory (filesystem ./shaders/ or archive
    // shaders/postprocess/).
    std::vector<PostProcessShaderExternalTexture> externalTextures;
    externalTextures.reserve(preset.textures.size());
    for (const auto& tex : preset.textures) {
        if (tex.name.empty() || tex.path.empty()) {
            SPDLOG_ERROR("Post-process preset '{}': textures entry missing name or path", name);
            return false;
        }
        std::vector<uint8_t> pngBytes;
        if (!ReadShaderBinaryFile(fsBase, arBase, tex.path, pngBytes)) {
            SPDLOG_ERROR("Post-process preset '{}' texture '{}': '{}' not found "
                         "(searched filesystem '{}' and archive '{}')",
                         name, tex.name, tex.path, fsBase, arBase);
            return false;
        }
        PostProcessShaderExternalTexture loaded;
        loaded.name = tex.name;
        loaded.filterLinear = tex.filterLinear;
        loaded.wrapMode = tex.wrapMode;
        if (!DecodeExternalTexture(name + "/" + tex.name, pngBytes, loaded)) {
            return false;
        }
        externalTextures.push_back(std::move(loaded));
    }

    out.name = name;
    out.sources.clear();
    out.configs.clear();
    out.externalTextures = std::move(externalTextures);
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
        out.sources.push_back(MakeSource(displayName, std::move(raw), aliasNames));
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
