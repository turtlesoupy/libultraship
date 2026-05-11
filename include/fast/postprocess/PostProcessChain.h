// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.3.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#pragma once

#include <cstdint>
#include <string>

#include "PostProcessTypes.h"

namespace Fast {
class GfxRenderingAPI;

// Owns the off-screen framebuffer(s) and compiled program slot for the
// post-process / user-shader pipeline. The interpreter holds one of
// these as a member and forwards Init / OnResize / Run calls from its
// existing per-frame entry points.
//
// Phase 1 only supports a single fragment pass, so the chain holds
// exactly one destination FBO whose color texture is what the GUI
// samples. Multi-pass (a ring of ping-pong FBOs) is a follow-up; the
// public API is shaped so adding more passes does not change the call
// sites in the interpreter.
class PostProcessChain {
  public:
    PostProcessChain() = default;
    ~PostProcessChain() = default;

    PostProcessChain(const PostProcessChain&) = delete;
    PostProcessChain& operator=(const PostProcessChain&) = delete;

    // Allocate backend FBOs. Safe to call against a backend that
    // returns SupportsPostProcess() == false; in that case the chain
    // stays disarmed and Run() is a no-op that returns srcFb unchanged.
    void Init(GfxRenderingAPI* rapi);

    // Resize the chain's FBOs to dstWidth x dstHeight. Idempotent.
    void OnResize(GfxRenderingAPI* rapi, uint32_t dstWidth, uint32_t dstHeight);

    // Compile + install a new program. Replaces any previously loaded
    // program. Returns true on success; on failure the chain is left
    // disarmed and Run() passes through.
    bool LoadShader(GfxRenderingAPI* rapi, const PostProcessSource& src);

    // Drop the loaded program and return to passthrough.
    void UnloadShader(GfxRenderingAPI* rapi);

    // Execute the pipeline. Returns the FBO id whose color texture
    // should be exposed to the GUI for the rest of the frame. If the
    // chain is inactive the return value is srcFb (a passthrough).
    int Run(GfxRenderingAPI* rapi, int srcFb, const PostProcessParams& params);

    // True iff a compiled program is loaded and the backend supports
    // post-process. The interpreter checks this to decide whether to
    // force the MSAA resolve to land in a sampleable texture (rather
    // than blitting directly to the swap-chain back buffer).
    bool IsActive() const;

    // Diagnostic accessor for the currently-loaded shader name.
    const std::string& GetLoadedName() const {
        return mLoadedName;
    }

  private:
    int mProgramId = -1;
    int mDstFb = -1;
    uint32_t mDstWidth = 0;
    uint32_t mDstHeight = 0;
    bool mBackendSupported = false;
    std::string mLoadedName;
};

} // namespace Fast
