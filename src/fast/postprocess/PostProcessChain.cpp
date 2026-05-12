// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.3
// and the libretro .glslp scale-type conventions documented at
// libretro/glsl-shaders. No code copied from RetroArch or any
// GPL-licensed shader runtime.
#include "fast/postprocess/PostProcessChain.h"

#include <algorithm>
#include <cmath>

#include "fast/backends/gfx_rendering_api.h"

namespace Fast {

namespace {

uint32_t AxisSize(PostProcessScaleType type, float scale, uint32_t source, uint32_t viewport) {
    float pixels = 1.0f;
    switch (type) {
        case PostProcessScaleType::Source:
            pixels = static_cast<float>(source) * scale;
            break;
        case PostProcessScaleType::Viewport:
            pixels = static_cast<float>(viewport) * scale;
            break;
        case PostProcessScaleType::Absolute:
            pixels = scale;
            break;
    }
    const long rounded = std::lround(pixels);
    if (rounded < 1) {
        return 1u;
    }
    return static_cast<uint32_t>(rounded);
}

} // namespace

void PostProcessChain::Init(GfxRenderingAPI* rapi) {
    if (rapi == nullptr) {
        return;
    }
    mBackendSupported = rapi->SupportsPostProcess();
    if (!mBackendSupported) {
        return;
    }
    mDstFb = rapi->CreateFramebuffer();
    mDstWidth = 0;
    mDstHeight = 0;
}

void PostProcessChain::OnResize(GfxRenderingAPI* rapi, uint32_t dstWidth, uint32_t dstHeight) {
    if (rapi == nullptr || !mBackendSupported || mDstFb < 0) {
        return;
    }
    if (dstWidth == 0 || dstHeight == 0) {
        return;
    }
    if (mDstWidth == dstWidth && mDstHeight == dstHeight) {
        return;
    }
    // Sampleable single-sample color, no depth — this is the final
    // output surface the GUI consumes via ImGui::Image.
    rapi->UpdateFramebufferParameters(mDstFb, dstWidth, dstHeight,
                                      /*msaa_level=*/1,
                                      /*opengl_invertY=*/false,
                                      /*render_target=*/true,
                                      /*has_depth_buffer=*/false,
                                      /*can_extract_depth=*/false);
    mDstWidth = dstWidth;
    mDstHeight = dstHeight;
}

bool PostProcessChain::LoadShader(GfxRenderingAPI* rapi, const PostProcessSource& src) {
    PostProcessPresetPass cfg; // Defaults: scaleType=Source, scale=1.0, filter=false.
    return LoadPasses(rapi, { src }, { cfg });
}

bool PostProcessChain::LoadPasses(GfxRenderingAPI* rapi,
                                  const std::vector<PostProcessSource>& sources,
                                  const std::vector<PostProcessPresetPass>& configs) {
    if (rapi == nullptr || !mBackendSupported) {
        return false;
    }
    if (sources.empty() || sources.size() != configs.size()) {
        return false;
    }

    // Compile all passes up front so a partial failure doesn't leave
    // the chain in a half-loaded state — if any pass fails, we tear
    // down what we built and keep the prior shader.
    std::vector<Pass> staged;
    staged.reserve(sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
        const int prog = rapi->CreatePostProcessProgram(sources[i]);
        if (prog < 0) {
            // Roll back the programs we already compiled.
            for (auto& p : staged) {
                rapi->DestroyPostProcessProgram(p.programId);
                if (p.outputFb >= 0) {
                    rapi->DestroyFramebuffer(p.outputFb);
                }
            }
            return false;
        }
        Pass p;
        p.programId = prog;
        p.config = configs[i];
        // Intermediate passes (not the last) own a chain-managed FBO.
        // The last pass writes into mDstFb so the GUI can sample it.
        if (i + 1 < sources.size()) {
            p.outputFb = rapi->CreateFramebuffer();
        }
        staged.push_back(std::move(p));
    }

    UnloadShader(rapi);
    mPasses = std::move(staged);
    mLoadedName = sources.front().name; // Preset / shader display name.
    return true;
}

void PostProcessChain::UnloadShader(GfxRenderingAPI* rapi) {
    if (rapi == nullptr) {
        return;
    }
    for (auto& p : mPasses) {
        if (p.programId >= 0 && mBackendSupported) {
            rapi->DestroyPostProcessProgram(p.programId);
        }
        if (p.outputFb >= 0 && mBackendSupported) {
            rapi->DestroyFramebuffer(p.outputFb);
        }
    }
    mPasses.clear();
    mLoadedName.clear();
}

int PostProcessChain::Run(GfxRenderingAPI* rapi, int srcFb, const PostProcessParams& params) {
    if (!IsActive() || mDstWidth == 0 || mDstHeight == 0) {
        return srcFb;
    }

    // The chain's "original" FB stays pinned to the game FB across
    // every pass — that's the sampler 1 binding (`Original`) libretro
    // multipass shaders use to combine the post-bloom buffer with the
    // pre-bloom source. Source rotates pass-to-pass; Original does not.
    const int originalFb = srcFb;
    const uint32_t originalWidth = params.srcWidth;
    const uint32_t originalHeight = params.srcHeight;

    int curIn = srcFb;
    uint32_t curW = params.srcWidth;
    uint32_t curH = params.srcHeight;
    const size_t lastIdx = mPasses.size() - 1;

    for (size_t i = 0; i < mPasses.size(); ++i) {
        Pass& p = mPasses[i];
        const bool isLast = (i == lastIdx);

        uint32_t outW, outH;
        int outFb;
        if (isLast) {
            // Last pass renders straight into the chain output, which
            // is already sized at viewport dimensions.
            outW = mDstWidth;
            outH = mDstHeight;
            outFb = mDstFb;
        } else {
            outW = AxisSize(p.config.scaleTypeX, p.config.scaleX, curW, params.dstWidth);
            outH = AxisSize(p.config.scaleTypeY, p.config.scaleY, curH, params.dstHeight);
            if (outW != p.lastWidth || outH != p.lastHeight) {
                rapi->UpdateFramebufferParameters(p.outputFb, outW, outH,
                                                  /*msaa_level=*/1,
                                                  /*opengl_invertY=*/false,
                                                  /*render_target=*/true,
                                                  /*has_depth_buffer=*/false,
                                                  /*can_extract_depth=*/false);
                p.lastWidth = outW;
                p.lastHeight = outH;
            }
            outFb = p.outputFb;
        }

        PostProcessParams pp = params;
        pp.srcWidth = curW;
        pp.srcHeight = curH;
        pp.originalWidth = originalWidth;
        pp.originalHeight = originalHeight;
        pp.dstWidth = outW;
        pp.dstHeight = outH;
        rapi->RunPostProcess(p.programId, curIn, outFb, originalFb, pp);

        curIn = outFb;
        curW = outW;
        curH = outH;
    }
    return mDstFb;
}

bool PostProcessChain::IsActive() const {
    return mBackendSupported && !mPasses.empty() && mDstFb >= 0;
}

} // namespace Fast
