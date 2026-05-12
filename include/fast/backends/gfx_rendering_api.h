#pragma once

#include <stdint.h>

#include <unordered_map>
#include <set>
#include "imconfig.h"
#include "../postprocess/PostProcessTypes.h"

namespace Fast {
struct ShaderProgram;

struct GfxClipParameters {
    bool z_is_from_0_to_1;
    bool invertY;
};

enum FilteringMode { FILTER_THREE_POINT, FILTER_LINEAR, FILTER_NONE };

// A hash function used to hash a: pair<float, float>
struct hash_pair_ff {
    size_t operator()(const std::pair<float, float>& p) const {
        const auto hash1 = std::hash<float>{}(p.first);
        const auto hash2 = std::hash<float>{}(p.second);

        // If hash1 == hash2, their XOR is zero.
        return (hash1 != hash2) ? hash1 ^ hash2 : hash1;
    }
};

class GfxRenderingAPI {
  public:
    virtual ~GfxRenderingAPI() = default;
    virtual const char* GetName() = 0;
    virtual int GetMaxTextureSize() = 0;
    virtual GfxClipParameters GetClipParameters() = 0;
    virtual void UnloadShader(ShaderProgram* oldPrg) = 0;
    virtual void LoadShader(ShaderProgram* newPrg) = 0;
    virtual ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint64_t shaderId1) = 0;
    virtual ShaderProgram* LookupShader(uint64_t shaderId0, uint64_t shaderId1) = 0;
    virtual void ShaderGetInfo(ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) = 0;
    virtual uint32_t NewTexture() = 0;
    virtual void SelectTexture(int tile, uint32_t textureId) = 0;
    virtual void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) = 0;
    virtual void SetSamplerParameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) = 0;
    virtual void SetDepthTestAndMask(bool depth_test, bool z_upd) = 0;
    virtual void SetZmodeDecal(bool decal) = 0;
    virtual void SetViewport(int x, int y, int width, int height) = 0;
    virtual void SetScissor(int x, int y, int width, int height) = 0;
    virtual void SetUseAlpha(bool useAlpha) = 0;
    virtual void DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) = 0;
    virtual void Init() = 0;
    virtual void OnResize() = 0;
    virtual void StartFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void FinishRender() = 0;
    virtual int CreateFramebuffer() = 0;
    // Release the backend resources backing `fbId` (color texture, depth
    // attachment, RTV, etc.) and mark the slot reusable so a later
    // CreateFramebuffer can return the same id. Calling Destroy on -1,
    // an out-of-range id, or an already-destroyed id is a no-op. The
    // default empty body keeps backends that haven't overridden it
    // source-compatible — they just leak, which matches the prior
    // behaviour of every backend before this hook existed.
    virtual void DestroyFramebuffer(int fbId) {
        (void)fbId;
    }
    virtual void UpdateFramebufferParameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                             bool opengl_invertY, bool render_target, bool has_depth_buffer,
                                             bool can_extract_depth) = 0;
    virtual void StartDrawToFramebuffer(int fbId, float noiseScale) = 0;
    virtual void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0,
                                 int dstY0, int dstX1, int dstY1) = 0;
    virtual void ClearFramebuffer(bool color, bool depth) = 0;
    virtual void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) = 0;
    virtual void ResolveMSAAColorBuffer(int fbIdTarger, int fbIdSrc) = 0;
    virtual std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fb_id, const std::set<std::pair<float, float>>& coordinates) = 0;
    virtual void* GetFramebufferTextureId(int fbId) = 0;
    virtual void SelectTextureFb(int fbId) = 0;
    // True iff sampling this FB's color attachment as a texture returns rows
    // in the opposite Y direction from the rendering side. The Fast3D
    // FB-as-texture passthrough hook (Interpreter::ImportTexture) uses this
    // to V-flip its FbUvTransform so consumer UVs stay consistent with the
    // game's intent across backends. Default false; OpenGL overrides to
    // return mFrameBuffers[fbId].invertY.
    virtual bool FbNeedsSampleVFlip(int fbId) {
        (void)fbId;
        return false;
    }
    virtual void DeleteTexture(uint32_t texId) = 0;
    virtual void SetTextureFilter(FilteringMode mode) = 0;
    virtual FilteringMode GetTextureFilter() = 0;
    virtual void SetSrgbMode() = 0;
    virtual ImTextureID GetTextureById(int id) = 0;

    // Post-process / user-shader runtime.
    //
    // Phase 1 of the CRT-shader plan (docs/crt_shader_plan_2026-05-11.md
    // §3.2). The interface is small on purpose: the interpreter owns
    // the FBO chain and the per-frame parameter pack; backends are only
    // responsible for compiling a fragment program and running it as a
    // fullscreen pass that samples a source FB's color texture and
    // writes into a destination FB.
    //
    // SupportsPostProcess() is the runtime gate. Backends that haven't
    // implemented the feature leave the default `return false` and the
    // interpreter skips the pass; backends that have implemented it
    // return true and the other three methods become live. This avoids
    // forcing every backend to ship a full implementation in lockstep.
    //
    // Returned program IDs are opaque to the interpreter — backends
    // can use vector indices, handle ints, whatever — but must be
    // round-trippable: a value handed back from CreatePostProcessProgram
    // is what the interpreter passes to RunPostProcess / Destroy.
    // -1 means "compile failed".
    virtual bool SupportsPostProcess() {
        return false;
    }
    virtual int CreatePostProcessProgram(const PostProcessSource& src) {
        (void)src;
        return -1;
    }
    virtual void DestroyPostProcessProgram(int progId) {
        (void)progId;
    }
    // `originalFb` is the un-modified game framebuffer the chain
    // started from — same fb id for every pass in a multi-pass chain.
    // Pass 0 callers should pass srcFb == originalFb. Backends bind it
    // to a second texture slot so shaders can reference it as
    // `Original` alongside the previous-pass `Source` at slot 0.
    virtual void RunPostProcess(int progId, int srcFb, int dstFb, int originalFb,
                                const PostProcessParams& params) {
        (void)progId;
        (void)srcFb;
        (void)dstFb;
        (void)originalFb;
        (void)params;
    }

  protected:
    int8_t mCurrentDepthTest = 0;
    int8_t mCurrentDepthMask = 0;
    int8_t mCurrentZmodeDecal = 0;
    int8_t mLastDepthTest = -1;
    int8_t mLastDepthMask = -1;
    int8_t mLastZmodeDecal = -1;
    bool mSrgbMode = false;
};
} // namespace Fast
