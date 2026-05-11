// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.3.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#include "fast/postprocess/PostProcessChain.h"

#include "fast/backends/gfx_rendering_api.h"

namespace Fast {

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
    if (rapi == nullptr || !mBackendSupported) {
        return false;
    }
    const int newProg = rapi->CreatePostProcessProgram(src);
    if (newProg < 0) {
        return false;
    }
    if (mProgramId >= 0) {
        rapi->DestroyPostProcessProgram(mProgramId);
    }
    mProgramId = newProg;
    mLoadedName = src.name;
    return true;
}

void PostProcessChain::UnloadShader(GfxRenderingAPI* rapi) {
    if (rapi == nullptr) {
        return;
    }
    if (mProgramId >= 0 && mBackendSupported) {
        rapi->DestroyPostProcessProgram(mProgramId);
    }
    mProgramId = -1;
    mLoadedName.clear();
}

int PostProcessChain::Run(GfxRenderingAPI* rapi, int srcFb, const PostProcessParams& params) {
    if (!IsActive() || mDstWidth == 0 || mDstHeight == 0) {
        return srcFb;
    }
    rapi->RunPostProcess(mProgramId, srcFb, mDstFb, params);
    return mDstFb;
}

bool PostProcessChain::IsActive() const {
    return mBackendSupported && mProgramId >= 0 && mDstFb >= 0;
}

} // namespace Fast
