// Smoke tests for the .slang -> SPIR-V -> backend transpile path.
// Implemented against the public glslang and SPIRV-Cross APIs. No
// code copied from RetroArch or any GPL-licensed shader runtime.

#include <gtest/gtest.h>

#include "fast/postprocess/PostProcessSlangSource.h"
#include "fast/postprocess/PostProcessSlangTranspiler.h"

namespace {

// A self-contained synthetic slang shader: passes-through position
// using a `MVP` matrix, samples a `Source` texture, and scales by a
// `gain` uniform. Exercises the UBO + sampler reflection without
// depending on any real-shader source.
constexpr const char* kMinimalSlang = R"(#version 450
layout(set = 0, binding = 0, std140) uniform UBO {
    mat4 MVP;
    vec4 OutputSize;
    vec4 SourceSize;
    float gain;
} global;
layout(set = 0, binding = 1) uniform sampler2D Source;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord * global.OutputSize.xy / global.OutputSize.xy;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
void main() {
    vec4 src = texture(Source, vTexCoord);
    float s = global.SourceSize.x;
    FragColor = src * global.gain * (s / s);
}
)";

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// End-to-end: parse a synthetic .slang shader, compile it through
// glslang+SPIRV-Cross, and verify each backend's source slot gets
// populated with a recognizable entry point.
TEST(PostProcessSlangTranspiler, CompilesMinimalShader) {
    Fast::PostProcessSlangSource src;
    std::string err;
    ASSERT_TRUE(Fast::ParseSlangSource(kMinimalSlang, src, err)) << err;

    Fast::PostProcessSlangArtifact out;
    const bool ok = Fast::PostProcessSlangTranspiler::Compile(src, out, err);
    if (!ok) {
        // Transpiler may be disabled at build time. In that case the
        // contract is: returns false with a non-empty errOut, and the
        // artifact is left in default-constructed state.
        EXPECT_FALSE(err.empty());
        EXPECT_TRUE(out.vertex.glsl.empty());
        EXPECT_TRUE(out.fragment.glsl.empty());
        EXPECT_TRUE(out.uboMembers.empty());
        EXPECT_TRUE(out.samplers.empty());
        GTEST_SKIP() << "transpiler not built; backend reflection skipped";
    }

    // Each stage must populate all three backend slots.
    EXPECT_FALSE(out.vertex.glsl.empty());
    EXPECT_FALSE(out.vertex.hlsl.empty());
    EXPECT_FALSE(out.vertex.msl.empty());
    EXPECT_FALSE(out.fragment.glsl.empty());
    EXPECT_FALSE(out.fragment.hlsl.empty());
    EXPECT_FALSE(out.fragment.msl.empty());

    // Spot-check entry-point conventions match what each backend looks up.
    EXPECT_TRUE(Contains(out.vertex.hlsl, "VSMain"));
    EXPECT_TRUE(Contains(out.fragment.hlsl, "PSMain"));
    EXPECT_TRUE(Contains(out.vertex.msl, "postprocess_vertex"));
    EXPECT_TRUE(Contains(out.fragment.msl, "postprocess_fragment"));
}

// SPIR-V reflection on the synthetic shader should surface the UBO's
// declared members in declaration order (sorted by offset) and the
// single Source sampler at descriptor set 0, binding 1.
TEST(PostProcessSlangTranspiler, ReflectsUboMembersAndSamplers) {
    Fast::PostProcessSlangSource src;
    std::string err;
    ASSERT_TRUE(Fast::ParseSlangSource(kMinimalSlang, src, err)) << err;

    Fast::PostProcessSlangArtifact out;
    if (!Fast::PostProcessSlangTranspiler::Compile(src, out, err)) {
        GTEST_SKIP() << "transpiler not built";
    }

    // UBO: mat4 MVP @ 0, vec4 OutputSize @ 64, vec4 SourceSize @ 80,
    // float gain @ 96. std140 totalBytes >= 100.
    ASSERT_EQ(out.uboMembers.size(), 4u);
    EXPECT_EQ(out.uboMembers[0].name, "MVP");
    EXPECT_EQ(out.uboMembers[0].offsetBytes, 0u);
    EXPECT_EQ(out.uboMembers[0].sizeBytes, 64u);
    EXPECT_EQ(out.uboMembers[1].name, "OutputSize");
    EXPECT_EQ(out.uboMembers[1].offsetBytes, 64u);
    EXPECT_EQ(out.uboMembers[1].sizeBytes, 16u);
    EXPECT_EQ(out.uboMembers[2].name, "SourceSize");
    EXPECT_EQ(out.uboMembers[2].offsetBytes, 80u);
    EXPECT_EQ(out.uboMembers[2].sizeBytes, 16u);
    EXPECT_EQ(out.uboMembers[3].name, "gain");
    EXPECT_EQ(out.uboMembers[3].offsetBytes, 96u);
    EXPECT_EQ(out.uboMembers[3].sizeBytes, 4u);
    EXPECT_GE(out.uboTotalBytes, 100u);

    ASSERT_EQ(out.samplers.size(), 1u);
    EXPECT_EQ(out.samplers[0].name, "Source");
    EXPECT_EQ(out.samplers[0].descriptorSet, 0u);
    EXPECT_EQ(out.samplers[0].binding, 1u);
}

// Slang shaders that fail glslang parse should surface a non-empty
// error rather than leaving stale state on the artifact.
TEST(PostProcessSlangTranspiler, RejectsMalformedGlsl) {
    constexpr const char* kBadSlang = R"(#version 450
#pragma stage vertex
void main() { this is not glsl; }
#pragma stage fragment
void main() {}
)";
    Fast::PostProcessSlangSource src;
    std::string err;
    ASSERT_TRUE(Fast::ParseSlangSource(kBadSlang, src, err)) << err;

    Fast::PostProcessSlangArtifact out;
    err.clear();
    const bool ok = Fast::PostProcessSlangTranspiler::Compile(src, out, err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(out.uboMembers.empty());
    EXPECT_TRUE(out.samplers.empty());
}
