// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.2.
// Authored against the public glslang and SPIRV-Cross APIs and their
// README/sample documentation. No code copied from RetroArch or any
// GPL-licensed shader runtime.

#include "fast/postprocess/PostProcessTranspiler.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#ifdef LUS_POSTPROCESS_TRANSPILER
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>
#endif

namespace Fast {

#ifdef LUS_POSTPROCESS_TRANSPILER

namespace {

// Schema declarations the user GLSL is expected to contain in loose-
// uniform / standalone-varying form. Each one is replaced with an
// explicit-binding Vulkan-GLSL declaration before glslang sees the
// source, so SPIRV-Cross emits a clean UBO + separate-sampler graph
// matching what the D3D11/Metal backends expect at runtime.
constexpr const char* kSchemaLineStarts[] = {
    "uniform sampler2D Source", "uniform sampler2D Original",
    "uniform vec2 SourceSize",  "uniform vec2 OutputSize",
    "uniform vec2 InputSize",   "uniform vec2 OriginalSize",
    "uniform int FrameCount",   "uniform float FrameDirection",
    "in vec2 vTexCoord",        "out vec4 fragColor",
};

// Resource bindings:
//   set=0, binding=0 — sampler2D Source   (previous-pass output)
//   set=0, binding=1 — sampler2D Original (game FB, multipass-only)
//   set=0, binding=2 — PostProcessUniforms UBO
//   set=0, binding=3+ — per-alias / external-texture sampler2D
// Sampler bindings 0/1 map directly to t0/s0 + t1/s1 (HLSL) and
// texture(0)/sampler(0) + texture(1)/sampler(1) (MSL); the UBO maps
// to b0 / buffer(0) after explicit remapping in the per-language
// compile steps below.
//
// The UBO's tail varies per pass: each alias / external-texture name
// gets an appended `vec2 <name>Size` slot in declaration order. The
// C++ runtime structs in each backend mirror this layout (std140
// places vec2 at offset that's a multiple of 8), so a backend that
// writes `<name>Size` at offset 40 + 8*i ends up populating the slot
// SPIRV-Cross emitted at the same byte offset.
constexpr const char* kInjectedHeader =
    "layout(set=0, binding=0) uniform sampler2D Source;\n"
    "layout(set=0, binding=1) uniform sampler2D Original;\n"
    "layout(set=0, binding=2, std140) uniform PostProcessUniforms {\n"
    "    vec2 SourceSize;\n"
    "    vec2 OutputSize;\n"
    "    vec2 InputSize;\n"
    "    vec2 OriginalSize;\n"
    "    int  FrameCount;\n"
    "    float FrameDirection;\n";

constexpr const char* kInjectedFooter =
    "};\n"
    "layout(location=0) in vec2 vTexCoord;\n"
    "layout(location=0) out vec4 fragColor;\n";

// Build the Vulkan-flavored preamble. The injected UBO block is closed
// after appending one `vec2 <alias>Size` member per alias name, so the
// transpiled HLSL / MSL output reserves the matching trailing bytes
// at the same std140-derived offsets the backends write.
std::string BuildInjectedDeclarations(const std::vector<std::string>& aliasNames) {
    std::string out;
    out.reserve(std::strlen(kInjectedHeader) + std::strlen(kInjectedFooter) +
                aliasNames.size() * 24);
    out += kInjectedHeader;
    for (const std::string& alias : aliasNames) {
        if (alias.empty()) {
            continue;
        }
        out += "    vec2 ";
        out += alias;
        out += "Size;\n";
    }
    out += kInjectedFooter;
    return out;
}

bool LineStartMatchesSchema(const std::string& trimmed) {
    for (const char* prefix : kSchemaLineStarts) {
        const size_t n = std::strlen(prefix);
        if (trimmed.size() < n || trimmed.compare(0, n, prefix) != 0) {
            continue;
        }
        // Only strip when the prefix terminates the identifier — otherwise
        // a user identifier that shares the prefix (`SourceSizeFoo`) would
        // be false-matched.
        if (trimmed.size() == n) {
            return true;
        }
        const char c = trimmed[n];
        if (c == ' ' || c == '\t' || c == ';' || c == '=') {
            return true;
        }
    }
    return false;
}

// Strip the user's `uniform sampler2D <alias>` and `uniform vec2
// <alias>Size` declarations — the Vulkan-flavored preamble injects
// binding-explicit + UBO-block versions of both. Only inspects lines
// starting with `uniform`.
bool LineStartMatchesAlias(const std::string& trimmed,
                           const std::vector<std::string>& aliasNames) {
    if (aliasNames.empty()) {
        return false;
    }
    constexpr const char* kSamplerPrefix = "uniform sampler2D ";
    constexpr size_t kSamplerPrefixLen = 18; // strlen("uniform sampler2D ")
    constexpr const char* kVec2Prefix = "uniform vec2 ";
    constexpr size_t kVec2PrefixLen = 13; // strlen("uniform vec2 ")

    auto matchAfterPrefix = [&](size_t prefixLen, const std::string& ident) -> bool {
        if (trimmed.size() < prefixLen + ident.size()) {
            return false;
        }
        if (trimmed.compare(prefixLen, ident.size(), ident) != 0) {
            return false;
        }
        const size_t end = prefixLen + ident.size();
        if (end == trimmed.size()) {
            return true;
        }
        const char c = trimmed[end];
        return c == ' ' || c == '\t' || c == ';' || c == '=';
    };

    if (trimmed.size() > kSamplerPrefixLen &&
        trimmed.compare(0, kSamplerPrefixLen, kSamplerPrefix) == 0) {
        for (const std::string& alias : aliasNames) {
            if (alias.empty()) {
                continue;
            }
            if (matchAfterPrefix(kSamplerPrefixLen, alias)) {
                return true;
            }
        }
        return false;
    }
    if (trimmed.size() > kVec2PrefixLen &&
        trimmed.compare(0, kVec2PrefixLen, kVec2Prefix) == 0) {
        for (const std::string& alias : aliasNames) {
            if (alias.empty()) {
                continue;
            }
            const std::string sizeIdent = alias + "Size";
            if (matchAfterPrefix(kVec2PrefixLen, sizeIdent)) {
                return true;
            }
        }
    }
    return false;
}

// Rewrite the user GLSL into a Vulkan-targeted form: strip user
// `#version` / schema declarations, prepend binding-explicit replacements.
std::string PreprocessForVulkan(const std::string& src,
                                const std::vector<std::string>& aliasNames) {
    std::istringstream in(src);
    std::string line;
    std::vector<std::string> body;
    body.reserve(src.size() / 32 + 16);
    while (std::getline(in, line)) {
        const size_t firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs != std::string::npos &&
            line.compare(firstNonWs, 8, "#version") == 0) {
            continue;
        }
        const std::string trimmed =
            (firstNonWs == std::string::npos) ? std::string() : line.substr(firstNonWs);
        if (LineStartMatchesSchema(trimmed)) {
            continue;
        }
        if (LineStartMatchesAlias(trimmed, aliasNames)) {
            continue;
        }
        body.push_back(line);
    }
    std::string out;
    out.reserve(src.size() + 512);
    out += "#version 450\n";
    out += BuildInjectedDeclarations(aliasNames);
    // Per-alias binding-explicit sampler declarations at slots 3, 4, ...
    // (Source=0, Original=1, PostProcessUniforms=2, aliases=3+).
    for (size_t i = 0; i < aliasNames.size(); ++i) {
        const std::string& alias = aliasNames[i];
        if (alias.empty()) {
            continue;
        }
        out += "layout(set=0, binding=";
        out += std::to_string(3 + i);
        out += ") uniform sampler2D ";
        out += alias;
        out += ";\n";
    }
    for (const auto& l : body) {
        out += l;
        out += '\n';
    }
    return out;
}

bool sGlslangInitialized = false;

bool EnsureGlslangInitialized() {
    if (sGlslangInitialized) {
        return true;
    }
    if (!glslang::InitializeProcess()) {
        return false;
    }
    sGlslangInitialized = true;
    return true;
}

bool CompileFragmentToSpirv(const std::string& vulkanSrc,
                            std::vector<unsigned int>& spirv, std::string& errOut) {
    if (!EnsureGlslangInitialized()) {
        errOut = "glslang::InitializeProcess() failed";
        return false;
    }
    glslang::TShader shader(EShLangFragment);
    const char* strs[] = { vulkanSrc.c_str() };
    shader.setStrings(strs, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangFragment, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setEntryPoint("main");
    shader.setSourceEntryPoint("main");
    const TBuiltInResource* resources = GetDefaultResources();
    if (!shader.parse(resources, 100, false, EShMsgDefault)) {
        errOut = std::string("GLSL parse failed: ") + shader.getInfoLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault)) {
        errOut = std::string("GLSL link failed: ") + program.getInfoLog();
        return false;
    }
    glslang::SpvOptions opts;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), spirv, &opts);
    if (spirv.empty()) {
        errOut = "SPIR-V module is empty";
        return false;
    }
    return true;
}

// Stock HLSL vertex stub. Emits a fullscreen triangle keyed off
// SV_VertexID with no vertex buffer; UVs flip Y so D3D's top-down V axis
// aligns with the clip-Y orientation the OpenGL/Metal stubs already use.
// Resource declarations (Source / SourceSampler / cbuffer) are left to
// the SPIRV-Cross fragment output that gets concatenated after this stub.
constexpr const char* kStockHlslVertex =
    "// Stock fullscreen-triangle vertex shader (LUS post-process).\n"
    "struct VOut {\n"
    "    float4 position : SV_Position;\n"
    "    float2 vTexCoord : TEXCOORD0;\n"
    "};\n"
    "\n"
    "VOut VSMain(uint vid : SV_VertexID) {\n"
    "    static const float2 positions[3] = {\n"
    "        float2(-1.0, -1.0),\n"
    "        float2( 3.0, -1.0),\n"
    "        float2(-1.0,  3.0),\n"
    "    };\n"
    "    static const float2 texCoords[3] = {\n"
    "        float2(0.0, 1.0),\n"
    "        float2(2.0, 1.0),\n"
    "        float2(0.0, -1.0),\n"
    "    };\n"
    "    VOut o;\n"
    "    o.position = float4(positions[vid], 0.0, 1.0);\n"
    "    o.vTexCoord = texCoords[vid];\n"
    "    return o;\n"
    "}\n\n";

bool TranspileHlsl(const std::vector<unsigned int>& spirv,
                   const std::vector<std::string>& aliasNames,
                   std::string& outHlsl, std::string& errOut) {
    try {
        spirv_cross::CompilerHLSL compiler(spirv);
        spirv_cross::CompilerHLSL::Options opts;
        opts.shader_model = 50;
        // Without this the HLSL backend hardcodes the FS entry name to
        // "main" regardless of rename_entry_point() — see
        // spirv_hlsl.cpp::emit_hlsl_entry_point.
        opts.use_entry_point_name = true;
        compiler.set_hlsl_options(opts);
        auto resources = compiler.get_shader_resources();
        for (auto& ubo : resources.uniform_buffers) {
            // PostProcessUniforms lands at register b0. Force the
            // binding regardless of what the preamble declared so
            // adding more buffer bindings later doesn't shift this one.
            compiler.set_decoration(ubo.id, spv::DecorationDescriptorSet, 0);
            compiler.set_decoration(ubo.id, spv::DecorationBinding, 0);
            compiler.set_name(ubo.id, "PostProcessUniforms");
        }
        for (auto& img : resources.sampled_images) {
            // Per-sampler register assignment. SPIRV-Cross splits each
            // combined sampler into Texture2D + SamplerState; both
            // take the same numeric register index in their respective
            // t* / s* spaces. Schema bindings: Source=0, Original=1.
            // Aliases land at 2 + alias_index, matching the order the
            // chain populates extraBindings[] at run time.
            const std::string& name = compiler.get_name(img.id);
            uint32_t slot = 0;
            if (name == "Original") {
                slot = 1;
            } else {
                for (size_t i = 0; i < aliasNames.size(); ++i) {
                    if (name == aliasNames[i]) {
                        slot = static_cast<uint32_t>(2 + i);
                        break;
                    }
                }
            }
            compiler.set_decoration(img.id, spv::DecorationDescriptorSet, 0);
            compiler.set_decoration(img.id, spv::DecorationBinding, slot);
        }
        compiler.rename_entry_point("main", "PSMain", spv::ExecutionModelFragment);
        outHlsl = compiler.compile();
        return true;
    } catch (const std::exception& e) {
        errOut = std::string("SPIRV-Cross HLSL: ") + e.what();
        return false;
    }
}

// Stock MSL vertex stub. [[user(locn0)]] on vTexCoord matches the
// fragment's expected stage_in location-0 attribute that SPIRV-Cross
// emits for our `layout(location=0) in vec2 vTexCoord;` injected
// declaration.
constexpr const char* kStockMslVertex =
    "// Stock fullscreen-triangle vertex shader (LUS post-process).\n"
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct PPVertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 vTexCoord [[user(locn0)]];\n"
    "};\n"
    "\n"
    "vertex PPVertexOut postprocess_vertex(uint vid [[vertex_id]]) {\n"
    "    const float2 positions[3] = {\n"
    "        float2(-1.0, -1.0),\n"
    "        float2( 3.0, -1.0),\n"
    "        float2(-1.0,  3.0),\n"
    "    };\n"
    "    const float2 texCoords[3] = {\n"
    "        float2(0.0, 1.0),\n"
    "        float2(2.0, 1.0),\n"
    "        float2(0.0, -1.0),\n"
    "    };\n"
    "    PPVertexOut o;\n"
    "    o.position = float4(positions[vid], 0.0, 1.0);\n"
    "    o.vTexCoord = texCoords[vid];\n"
    "    return o;\n"
    "}\n\n";

bool TranspileMsl(const std::vector<unsigned int>& spirv,
                  const std::vector<std::string>& aliasNames,
                  std::string& outMsl, std::string& errOut) {
    try {
        spirv_cross::CompilerMSL compiler(spirv);
        spirv_cross::CompilerMSL::Options opts;
        opts.platform = spirv_cross::CompilerMSL::Options::macOS;
        opts.set_msl_version(2, 2);
        opts.argument_buffers = false;
        compiler.set_msl_options(opts);

        // Force Metal-backend bindings: Source at texture(0)+sampler(0),
        // Original at texture(1)+sampler(1), UBO at buffer(0). The
        // Metal RunPostProcess code hard-codes the same indices.
        spirv_cross::MSLResourceBinding sourceBind{};
        sourceBind.stage = spv::ExecutionModelFragment;
        sourceBind.desc_set = 0;
        sourceBind.binding = 0;
        sourceBind.msl_texture = 0;
        sourceBind.msl_sampler = 0;
        sourceBind.msl_buffer = 0;
        compiler.add_msl_resource_binding(sourceBind);

        spirv_cross::MSLResourceBinding originalBind{};
        originalBind.stage = spv::ExecutionModelFragment;
        originalBind.desc_set = 0;
        originalBind.binding = 1;
        originalBind.msl_texture = 1;
        originalBind.msl_sampler = 1;
        originalBind.msl_buffer = 0;
        compiler.add_msl_resource_binding(originalBind);

        spirv_cross::MSLResourceBinding ubo{};
        ubo.stage = spv::ExecutionModelFragment;
        ubo.desc_set = 0;
        ubo.binding = 2;
        ubo.msl_buffer = 0;
        ubo.msl_texture = 0;
        ubo.msl_sampler = 0;
        compiler.add_msl_resource_binding(ubo);

        // Aliases at binding=3+i (Vulkan) → texture(2+i)+sampler(2+i)
        // (MSL flat slot space). The Metal RunPostProcess loop binds
        // extraBindings[i] to the same slot order.
        for (size_t i = 0; i < aliasNames.size(); ++i) {
            spirv_cross::MSLResourceBinding aliasBind{};
            aliasBind.stage = spv::ExecutionModelFragment;
            aliasBind.desc_set = 0;
            aliasBind.binding = static_cast<uint32_t>(3 + i);
            aliasBind.msl_texture = static_cast<uint32_t>(2 + i);
            aliasBind.msl_sampler = static_cast<uint32_t>(2 + i);
            aliasBind.msl_buffer = 0;
            compiler.add_msl_resource_binding(aliasBind);
        }

        auto resources = compiler.get_shader_resources();
        for (auto& uboRes : resources.uniform_buffers) {
            compiler.set_name(uboRes.id, "PostProcessUniforms");
        }
        compiler.rename_entry_point("main", "postprocess_fragment", spv::ExecutionModelFragment);
        outMsl = compiler.compile();
        return true;
    } catch (const std::exception& e) {
        errOut = std::string("SPIRV-Cross MSL: ") + e.what();
        return false;
    }
}

} // namespace

bool PostProcessTranspiler::SynthesizeMissing(PostProcessSource& inout, std::string& errOut) {
    if (inout.glsl.empty()) {
        errOut = "no GLSL source to transpile";
        return false;
    }
    const bool wantHlsl = inout.hlsl.empty();
    const bool wantMsl = inout.msl.empty();
    if (!wantHlsl && !wantMsl) {
        return true;
    }

    const std::string vulkanSrc = PreprocessForVulkan(inout.glsl, inout.aliasNames);
    std::vector<unsigned int> spirv;
    if (!CompileFragmentToSpirv(vulkanSrc, spirv, errOut)) {
        return false;
    }

    if (wantHlsl) {
        std::string hlslFs;
        if (!TranspileHlsl(spirv, inout.aliasNames, hlslFs, errOut)) {
            return false;
        }
        inout.hlsl.clear();
        inout.hlsl.append(kStockHlslVertex);
        inout.hlsl.append(hlslFs);
    }
    if (wantMsl) {
        std::string mslFs;
        if (!TranspileMsl(spirv, inout.aliasNames, mslFs, errOut)) {
            return false;
        }
        inout.msl.clear();
        inout.msl.append(kStockMslVertex);
        inout.msl.append(mslFs);
    }
    return true;
}

#else  // LUS_POSTPROCESS_TRANSPILER

bool PostProcessTranspiler::SynthesizeMissing(PostProcessSource& /*inout*/, std::string& errOut) {
    errOut = "post-process transpiler disabled at build time";
    return false;
}

#endif // LUS_POSTPROCESS_TRANSPILER

} // namespace Fast
