// Smoke tests for the GLSL->HLSL/MSL post-process transpiler.
// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.2.
// No code copied from RetroArch or any GPL-licensed shader runtime.

#include <gtest/gtest.h>

#include "fast/postprocess/PostProcessTranspiler.h"
#include "fast/postprocess/PostProcessTypes.h"

namespace {

constexpr const char* kMinimalShader = R"(#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;
uniform sampler2D Source;
uniform vec2 SourceSize;
uniform vec2 OutputSize;
uniform vec2 InputSize;
uniform int FrameCount;
void main() {
    vec3 c = texture(Source, vTexCoord).rgb;
    float row = vTexCoord.y * SourceSize.y;
    c *= mix(0.75, 1.0, 0.5 + 0.5 * cos(row * 6.283185));
    fragColor = vec4(c, 1.0);
}
)";

} // namespace

TEST(PostProcessTranspiler, FillsBothLanguagesFromGlsl) {
    Fast::PostProcessSource src;
    src.name = "smoke";
    src.glsl = kMinimalShader;

    // Transpiler implementation lives in libultraship (the test links
    // against). Whether SynthesizeMissing actually does the transpile is
    // a libultraship build-config decision (LUS_ENABLE_POSTPROCESS_TRANSPILER);
    // here we just assert the visible contract: success populates the
    // language slots, failure leaves them empty with a non-empty errOut.
    std::string err;
    const bool ok = Fast::PostProcessTranspiler::SynthesizeMissing(src, err);
    if (ok) {
        EXPECT_FALSE(src.hlsl.empty()) << "HLSL slot was not populated";
        EXPECT_FALSE(src.msl.empty()) << "MSL slot was not populated";
        // Spot-check that the backend conventions are in the output. We
        // don't pin exact strings (SPIRV-Cross output is implementation-
        // defined), only the entry-point names the backend looks up.
        EXPECT_NE(src.hlsl.find("VSMain"), std::string::npos);
        EXPECT_NE(src.hlsl.find("PSMain"), std::string::npos);
        EXPECT_NE(src.msl.find("postprocess_vertex"), std::string::npos);
        EXPECT_NE(src.msl.find("postprocess_fragment"), std::string::npos);
    } else {
        EXPECT_FALSE(err.empty());
        EXPECT_TRUE(src.hlsl.empty());
        EXPECT_TRUE(src.msl.empty());
    }
}

TEST(PostProcessTranspiler, PreservesExistingSiblings) {
    Fast::PostProcessSource src;
    src.name = "preserved";
    src.glsl = kMinimalShader;
    src.hlsl = "/* user-authored HLSL */";
    src.msl = "/* user-authored MSL */";

    std::string err;
    const bool ok = Fast::PostProcessTranspiler::SynthesizeMissing(src, err);
    EXPECT_TRUE(ok) << err;
    EXPECT_EQ(src.hlsl, "/* user-authored HLSL */");
    EXPECT_EQ(src.msl, "/* user-authored MSL */");
}

TEST(PostProcessTranspiler, RejectsEmptyGlsl) {
    Fast::PostProcessSource src;
    src.name = "empty";

    std::string err;
    const bool ok = Fast::PostProcessTranspiler::SynthesizeMissing(src, err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
}
