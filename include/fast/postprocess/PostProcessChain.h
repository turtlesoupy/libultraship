// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.3.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PostProcessPreset.h"
#include "PostProcessTypes.h"

namespace Fast {
class GfxRenderingAPI;

// Owns the off-screen framebuffer(s) and compiled programs for the
// post-process / user-shader pipeline. The interpreter holds one of
// these and forwards Init / OnResize / Run calls from its existing
// per-frame entry points.
//
// Phase 1 was single-pass: one program, one output FBO. Phase 2
// generalizes to N passes wired into a linear chain. Each pass owns
// its own compiled program plus (for intermediates) its own
// sampleable FBO. The final pass writes into the chain's mDstFb
// (sized at viewport dimensions) — that's the texture the GUI
// samples via ImGui::Image.
//
// Per-pass FBO sizing follows the libretro .glslp convention: each
// pass declares scale_type / scale per-axis, computed off either the
// previous pass output size (`source`), the viewport size
// (`viewport`), or an absolute pixel count (`absolute`).
class PostProcessChain {
  public:
    PostProcessChain() = default;
    ~PostProcessChain() = default;

    PostProcessChain(const PostProcessChain&) = delete;
    PostProcessChain& operator=(const PostProcessChain&) = delete;

    // Allocate the chain's final-output FBO. Safe against a backend
    // that returns SupportsPostProcess() == false; in that case the
    // chain stays disarmed and Run() is a no-op that returns srcFb.
    void Init(GfxRenderingAPI* rapi);

    // Resize the chain's output FBO to viewport dims. Idempotent.
    // Intermediate FBOs are sized per-frame in Run() because their
    // dimensions depend on the runtime input/viewport sizes.
    void OnResize(GfxRenderingAPI* rapi, uint32_t dstWidth, uint32_t dstHeight);

    // Single-pass convenience: equivalent to LoadPasses with one
    // entry and default config (scale_type=source, scale=1.0).
    // Preserves the call site the interpreter uses for plain `.glsl`.
    bool LoadShader(GfxRenderingAPI* rapi, const PostProcessSource& src);

    // Multi-pass entry. `sources[i]` is the loaded + normalized +
    // transpiled GLSL for pass i; `configs[i]` carries that pass's
    // scale/filter/etc. metadata. The two vectors must be the same
    // size and at least 1 entry. Returns true on success; on failure
    // the chain is left in its prior state.
    bool LoadPasses(GfxRenderingAPI* rapi,
                    const std::vector<PostProcessSource>& sources,
                    const std::vector<PostProcessPresetPass>& configs);

    // Tear down all passes, releasing programs and intermediate FBOs.
    // Drop back to passthrough. The chain's final-output FBO (mDstFb)
    // is kept around for reuse on the next load.
    void UnloadShader(GfxRenderingAPI* rapi);

    // Execute the pipeline. Returns the FBO id whose color texture
    // should be exposed to the GUI for the rest of the frame. If the
    // chain is inactive the return value is srcFb (a passthrough).
    int Run(GfxRenderingAPI* rapi, int srcFb, const PostProcessParams& params);

    // True iff at least one pass is loaded and the backend supports
    // post-process. The interpreter checks this to decide whether to
    // force the MSAA resolve to land in a sampleable texture (rather
    // than blitting directly to the swap-chain back buffer).
    bool IsActive() const;

    // Diagnostic accessor for the currently-loaded preset / shader name.
    const std::string& GetLoadedName() const {
        return mLoadedName;
    }

  private:
    struct Pass {
        int programId = -1;
        // For intermediate passes (all but the last), `outputFb` is
        // a chain-owned FBO sized per the pass's scale config. The
        // final pass writes into the chain's mDstFb instead, leaving
        // its `outputFb` at -1.
        int outputFb = -1;
        // Last applied size for `outputFb`, so we skip
        // UpdateFramebufferParameters when the size hasn't changed.
        uint32_t lastWidth = 0;
        uint32_t lastHeight = 0;
        PostProcessPresetPass config;
    };

    // libretro `aliasN` produces a named binding sampleable by any
    // later pass. Stored in declaration order so binding-slot indices
    // (2 + i) stay stable from compile time through every Run call.
    // `producerPassIdx` is the index in mPasses whose output the
    // alias names — if a later alias name shadows an earlier one,
    // libretro's spec leaves the result undefined; we keep first-
    // declared wins.
    struct Alias {
        std::string name;
        size_t      producerPassIdx;
    };

    std::vector<Pass> mPasses;
    std::vector<Alias> mAliases;
    int mDstFb = -1;
    uint32_t mDstWidth = 0;
    uint32_t mDstHeight = 0;
    bool mBackendSupported = false;
    std::string mLoadedName;
};

} // namespace Fast
