#pragma once

#include "stdint.h"
#include "fast/ucodehandlers.h"

#ifdef __cplusplus
extern "C" {
#endif

void GfxSetNativeDimensions(uint32_t width, uint32_t height);
void GfxGetPixelDepthPrepare(float x, float y);
uint16_t GfxGetPixelDepth(float x, float y);

// SSB64 port hook: when set non-zero, Interpreter::AdjXForAspectRatio
// compresses post-projection clip-space X by (4/3) / window_aspect to
// expand the visible 4:3 frustum into the wider window. Default 0 ⇒
// AdjXForAspectRatio is a no-op (4:3 content stretches to fill window).
void GfxSetWidescreenActive(int active);

#ifdef __cplusplus
}
#endif
