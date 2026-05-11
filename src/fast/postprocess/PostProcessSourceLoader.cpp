// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.4.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#include "fast/postprocess/PostProcessSourceLoader.h"

#include <cstddef>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>

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

bool LoadPostProcessShader(const std::string& name, PostProcessSource& out) {
    if (name.empty()) {
        return false;
    }
    std::string text;
    const std::string fsPath = "shaders/" + name + ".glsl";
    if (ReadFilesystemFile(fsPath, text)) {
        out.name = name;
        out.glsl = std::move(text);
        return true;
    }
    const std::string arPath = "shaders/postprocess/" + name + ".glsl";
    if (ReadArchiveFile(arPath, text)) {
        out.name = name;
        out.glsl = std::move(text);
        return true;
    }
    SPDLOG_ERROR("Post-process shader '{}' not found (tried '{}' and archive '{}')", name, fsPath, arPath);
    return false;
}

std::vector<std::string> ListBuiltinPostProcessShaders() {
    // The list mirrors what GenerateF3DO2R packages from
    // libultraship/src/fast/shaders/postprocess/. Hardcoded rather than
    // enumerated at runtime so the menu picker stays stable across
    // archive contents and so a port that ships a stripped f3d.o2r still
    // gets a usable default selection.
    return { "scanlines" };
}

} // namespace Fast
