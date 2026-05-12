// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3.3.
// No code copied from RetroArch or any GPL-licensed shader runtime.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PostProcessPreset.h"
#include "PostProcessSourceLoader.h"
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
    // size and at least 1 entry. `externalTextures` is an optional
    // list of pre-decoded RGBA8 images (libretro `.glslp` `textures
    // = "..."` declarations) — uploaded once to the backend via
    // CreatePostProcessStaticTexture and bound per-pass at the
    // sampler slot the transpiler reserved for each name. Returns
    // true on success; on failure the chain is left in its prior
    // state (any partial backend resources allocated during the call
    // are rolled back).
    bool LoadPasses(GfxRenderingAPI* rapi,
                    const std::vector<PostProcessSource>& sources,
                    const std::vector<PostProcessPresetPass>& configs,
                    const std::vector<PostProcessShaderExternalTexture>& externalTextures = {});

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

    // Phase 2.3: per-pass `#pragma parameter` slider state. The chain
    // owns one float per declared parameter, initialized from the
    // pragma's default. The picker UI reads the descriptor list to
    // build sliders and writes back values via SetParameterValue.
    //
    // Indexing: parameter (passIdx, paramIdx) maps to mPasses[passIdx]
    // .parameterValues[paramIdx] and the descriptor comes from the
    // matching `mPasses[passIdx].parameterDescs[paramIdx]` (a snapshot
    // of PostProcessSource::parameters made at load time). The two
    // arrays share length per pass.
    size_t GetPassCount() const {
        return mPasses.size();
    }
    size_t GetParameterCount(size_t passIdx) const;
    // Returns nullptr for out-of-range indices.
    const PostProcessShaderParameter* GetParameterDescriptor(size_t passIdx, size_t paramIdx) const;
    float GetParameterValue(size_t passIdx, size_t paramIdx) const;
    void  SetParameterValue(size_t passIdx, size_t paramIdx, float value);

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
        // Phase 2.2: true when this pass's output is consumed by a
        // downstream pass that has `mipmap_input = true` and therefore
        // needs the producer texture allocated with mip storage. Set
        // during LoadPasses by walking the config list. The chain calls
        // SetPostProcessFramebufferMipmapped on the producer's outputFb
        // so backends that allocate immutable mip levels (D3D11 /
        // Metal) reserve storage before the first
        // UpdateFramebufferParameters.
        bool outputMipmapped = false;
        PostProcessPresetPass config;
        // Phase 2.3: per-pass `#pragma parameter` state snapshot
        // (lengths must match). The descs are an immutable copy of
        // PostProcessSource::parameters taken at LoadPasses time;
        // values are mutable and seeded from each descriptor's
        // defaultValue. The chain pushes &parameterValues[0] into
        // PostProcessParams.parameters every RunPostProcess.
        std::vector<PostProcessShaderParameter> parameterDescs;
        std::vector<float> parameterValues;
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

    // One uploaded external-texture slot.
    struct ExternalTexture {
        std::string name;
        int         textureId    = -1;
        uint32_t    width        = 1;
        uint32_t    height       = 1;
        bool        filterLinear = true;
        PostProcessWrapMode wrapMode = PostProcessWrapMode::ClampToEdge;
    };

    std::vector<Pass> mPasses;
    std::vector<Alias> mAliases;
    std::vector<ExternalTexture> mExternalTextures;
    int mDstFb = -1;
    uint32_t mDstWidth = 0;
    uint32_t mDstHeight = 0;
    bool mBackendSupported = false;
    std::string mLoadedName;
};

} // namespace Fast
