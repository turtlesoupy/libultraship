// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.4.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#include "fast/postprocess/PostProcessSourceLoader.h"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>

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

} // namespace

namespace {

// Populate one language slot in `out`. Looks first on the filesystem (where a
// user drops a sibling file next to the `.glsl` they authored), then in
// f3d.o2r for the bundled builtins. Silently no-ops if no file is found —
// the SPIRV-Cross transpiler will fill the gap once it lands, and backends
// that need a non-GLSL source check for emptiness and bail with a clear log.
void TryLoadLanguage(const std::string& name, const std::string& extension, std::string& outText) {
    if (ReadFilesystemFile("shaders/" + name + "." + extension, outText)) {
        return;
    }
    ReadArchiveFile("shaders/postprocess/" + name + "." + extension, outText);
}

} // namespace

bool LoadPostProcessShader(const std::string& name, PostProcessSource& out) {
    if (name.empty()) {
        return false;
    }
    std::string glsl;
    const std::string fsPath = "shaders/" + name + ".glsl";
    if (!ReadFilesystemFile(fsPath, glsl)) {
        const std::string arPath = "shaders/postprocess/" + name + ".glsl";
        if (!ReadArchiveFile(arPath, glsl)) {
            SPDLOG_ERROR("Post-process shader '{}' not found (tried '{}' and archive '{}')", name, fsPath, arPath);
            return false;
        }
    }
    out.name = name;
    out.glsl = std::move(glsl);
    // Hand-written backend-specific siblings, if present, win over the
    // transpiler output (they're treated as authoritative — useful when a
    // shader author wants tighter control of the HLSL/MSL emit). If a
    // sibling is missing, SynthesizeMissing fills the slot from the GLSL.
    //
    // Setting LUS_FORCE_POSTPROCESS_TRANSPILE in the environment skips the
    // sibling lookups so the transpile path always runs — handy for
    // smoke-testing transpiler changes against the bundled builtins.
    const bool forceTranspile = std::getenv("LUS_FORCE_POSTPROCESS_TRANSPILE") != nullptr;
    if (!forceTranspile) {
        TryLoadLanguage(name, "msl", out.msl);
        TryLoadLanguage(name, "hlsl", out.hlsl);
    }
    if (out.hlsl.empty() || out.msl.empty()) {
        std::string err;
        if (!PostProcessTranspiler::SynthesizeMissing(out, err)) {
            // Non-fatal: backends that need the missing slot will log a
            // clearer error of their own at compile time. The OpenGL
            // backend only consumes `out.glsl` so it stays unaffected.
            SPDLOG_WARN("Post-process shader '{}' could not be transpiled: {}", name, err);
        }
    }
    return true;
}

std::vector<std::string> ListBuiltinPostProcessShaders() {
    // The list mirrors what GenerateF3DO2R packages from
    // libultraship/src/fast/shaders/postprocess/. Hardcoded rather than
    // enumerated at runtime so the menu picker stays stable across
    // archive contents and so a port that ships a stripped f3d.o2r still
    // gets a usable default selection.
    return { "scanlines", "crt-lottes" };
}

} // namespace Fast
