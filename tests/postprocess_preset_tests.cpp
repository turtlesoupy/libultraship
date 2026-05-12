// Implemented against the libretro `.glslp` examples in
// libretro/glsl-shaders. No code copied from RetroArch or any
// GPL-licensed shader runtime.

#include <gtest/gtest.h>

#include "fast/postprocess/PostProcessPreset.h"

TEST(PostProcessPreset, ParsesSinglePassAperture) {
    constexpr const char* kSrc = R"(shaders = 1

shader0 = shaders/crt-aperture.glsl
filter_linear0 = false
)";
    Fast::PostProcessPreset preset;
    std::string err;
    ASSERT_TRUE(Fast::ParsePostProcessPreset(kSrc, "/tmp/presets", preset, err)) << err;
    EXPECT_EQ(preset.baseDir, "/tmp/presets");
    ASSERT_EQ(preset.passes.size(), 1u);
    EXPECT_EQ(preset.passes[0].shaderPath, "shaders/crt-aperture.glsl");
    EXPECT_FALSE(preset.passes[0].filterLinear);
    EXPECT_EQ(preset.passes[0].scaleX, 1.0f);
    EXPECT_EQ(preset.passes[0].scaleY, 1.0f);
}

TEST(PostProcessPreset, ParsesFivePassEasymodeHalation) {
    constexpr const char* kSrc = R"(shaders = "5"

shader0 = "shaders/crt-easymode-halation/linearize.glsl"
filter_linear0 = "false"
srgb_framebuffer0 = "true"
scale_type_x0 = "source"
scale_x0 = "1.000000"
scale_type_y0 = "source"
scale_y0 = "1.000000"

shader1 = "shaders/crt-easymode-halation/blur_horiz.glsl"
filter_linear1 = "false"
srgb_framebuffer1 = "true"
scale_type_x1 = "source"
scale_x1 = "1.000000"
scale_type_y1 = "source"
scale_y1 = "1.000000"

shader2 = "shaders/crt-easymode-halation/blur_vert.glsl"
filter_linear2 = "false"
srgb_framebuffer2 = "true"
scale_type_x2 = "source"
scale_x2 = "1.000000"
scale_type_y2 = "source"
scale_y2 = "1.000000"

shader3 = "shaders/crt-easymode-halation/threshold.glsl"
filter_linear3 = "false"
srgb_framebuffer3 = "true"
scale_type_x3 = "source"
scale_x3 = "1.000000"
scale_type_y3 = "source"
scale_y3 = "1.000000"

shader4 = "shaders/crt-easymode-halation/crt-easymode-halation.glsl"
filter_linear4 = "true"
)";

    Fast::PostProcessPreset preset;
    std::string err;
    ASSERT_TRUE(Fast::ParsePostProcessPreset(kSrc, "", preset, err)) << err;
    ASSERT_EQ(preset.passes.size(), 5u);

    for (int i = 0; i < 4; ++i) {
        EXPECT_FALSE(preset.passes[i].filterLinear) << "pass " << i;
        EXPECT_TRUE(preset.passes[i].srgbFramebuffer) << "pass " << i;
        EXPECT_EQ(preset.passes[i].scaleTypeX, Fast::PostProcessScaleType::Source);
        EXPECT_EQ(preset.passes[i].scaleTypeY, Fast::PostProcessScaleType::Source);
        EXPECT_FLOAT_EQ(preset.passes[i].scaleX, 1.0f);
        EXPECT_FLOAT_EQ(preset.passes[i].scaleY, 1.0f);
    }
    EXPECT_TRUE(preset.passes[4].filterLinear);
    // Pass 4 has no explicit scale; defaults should kick in.
    EXPECT_EQ(preset.passes[4].scaleTypeX, Fast::PostProcessScaleType::Source);
    EXPECT_FLOAT_EQ(preset.passes[4].scaleX, 1.0f);

    EXPECT_EQ(preset.passes[0].shaderPath, "shaders/crt-easymode-halation/linearize.glsl");
    EXPECT_EQ(preset.passes[4].shaderPath,
              "shaders/crt-easymode-halation/crt-easymode-halation.glsl");
}

TEST(PostProcessPreset, AcceptsCombinedScaleTypeAndAxisOverride) {
    constexpr const char* kSrc = R"(shaders = 1
shader0 = a.glsl
scale_type0 = absolute
scale_type_x0 = viewport
scale_x0 = 0.5
scale_y0 = 100
)";
    Fast::PostProcessPreset preset;
    std::string err;
    ASSERT_TRUE(Fast::ParsePostProcessPreset(kSrc, "", preset, err)) << err;
    ASSERT_EQ(preset.passes.size(), 1u);
    // scale_type0 set both to Absolute; scale_type_x0 overrides X to Viewport.
    EXPECT_EQ(preset.passes[0].scaleTypeX, Fast::PostProcessScaleType::Viewport);
    EXPECT_EQ(preset.passes[0].scaleTypeY, Fast::PostProcessScaleType::Absolute);
    EXPECT_FLOAT_EQ(preset.passes[0].scaleX, 0.5f);
    EXPECT_FLOAT_EQ(preset.passes[0].scaleY, 100.0f);
}

TEST(PostProcessPreset, StripsCommentsAndIgnoresUnknownKeys) {
    constexpr const char* kSrc = R"(# leading comment
shaders = 1
shader0 = "x.glsl"   # trailing comment after value
filter_linear0 = true
parameters = "FOO;BAR"
wrap_mode0 = clamp_to_border
)";
    Fast::PostProcessPreset preset;
    std::string err;
    ASSERT_TRUE(Fast::ParsePostProcessPreset(kSrc, "", preset, err)) << err;
    ASSERT_EQ(preset.passes.size(), 1u);
    EXPECT_TRUE(preset.passes[0].filterLinear);
}

// Libretro spec treats `shaders=N` as informational; if a preset over-
// declares the count and the trailing entries have no shaderN, we just
// trim. A gap in the middle (shader0 and shader2 set, shader1 missing)
// is unambiguously broken and should reject.
TEST(PostProcessPreset, TolaratesOverDeclaredCount) {
    constexpr const char* kSrc = R"(shaders = 3
shader0 = x.glsl
)";
    Fast::PostProcessPreset preset;
    std::string err;
    ASSERT_TRUE(Fast::ParsePostProcessPreset(kSrc, "", preset, err)) << err;
    EXPECT_EQ(preset.passes.size(), 1u);
}

TEST(PostProcessPreset, RejectsGapInPassNumbering) {
    constexpr const char* kSrc = R"(shaders = 3
shader0 = a.glsl
shader2 = c.glsl
)";
    Fast::PostProcessPreset preset;
    std::string err;
    EXPECT_FALSE(Fast::ParsePostProcessPreset(kSrc, "", preset, err));
    EXPECT_NE(err.find("missing 'shader1'"), std::string::npos);
}

TEST(PostProcessPreset, RejectsLineWithoutEquals) {
    constexpr const char* kSrc = "shaders = 1\nshader0 a.glsl\n";
    Fast::PostProcessPreset preset;
    std::string err;
    EXPECT_FALSE(Fast::ParsePostProcessPreset(kSrc, "", preset, err));
    EXPECT_NE(err.find("missing '='"), std::string::npos);
}
