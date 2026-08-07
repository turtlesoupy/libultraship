#include "ship/audio/SDLAudioPlayer.h"
#include <spdlog/spdlog.h>

namespace Ship {

SDLAudioPlayer::~SDLAudioPlayer() {
    SPDLOG_TRACE("destruct SDL audio player");
    DoClose();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDLAudioPlayer::DoClose() {
    if (mDevice != 0) {
        // Pause playback first
        SDL_PauseAudioDevice(mDevice, 1);
        // Clear any queued audio to prevent glitches when reopening
        SDL_ClearQueuedAudio(mDevice);
        SDL_CloseAudioDevice(mDevice);
        mDevice = 0;
    }
}

bool SDLAudioPlayer::DoInit() {
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        SPDLOG_ERROR("SDL init error: {}", SDL_GetError());
        return false;
    }

    // Always open with the correct number of output channels
    mNumChannels = this->GetNumOutputChannels();

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = this->GetSampleRate();
    want.format = AUDIO_S16SYS;
    want.channels = mNumChannels;
    want.samples = this->GetSampleLength();
    want.callback = NULL;

    mDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (mDevice == 0) {
        SPDLOG_ERROR("SDL_OpenAudio error: {}", SDL_GetError());
        return false;
    }

    SPDLOG_INFO("SDL Audio initialized: {} channels, {} Hz", mNumChannels, this->GetSampleRate());

#ifndef __EMSCRIPTEN__
    SDL_PauseAudioDevice(mDevice, 0);
#else
    /* WASM: leave the device paused until the first real submission
     * (DoPlay). Unpausing here lets SDL's queue-drain callback fire
     * JS->wasm during the boot chain's deep Asyncify fiber unwinds,
     * which can livelock the unwind state machine — the boot hang was
     * timing-dependent (any change to startup work moved it). Once the
     * frame loop is running, unwinds are shallow and the callback is
     * safe. */
#endif
    return true;
}

int SDLAudioPlayer::Buffered() {
    return SDL_GetQueuedAudioSize(mDevice) / (sizeof(int16_t) * mNumChannels);
}

#ifdef __EMSCRIPTEN__
/* Boot gate for the deferred unpause (see Init). SDL's emscripten audio
 * registers a wasm-side drain callback (HandleAudioProcess) the moment the
 * device unpauses; if it fires while the boot chain's deep Asyncify fiber
 * swaps are mid-flight, the reentrancy corrupts the asyncify state
 * (observed as timing-dependent boot livelocks / EM_ASM asserts). DoPlay
 * alone is not a safe gate — the audio FIBER submits during boot. The
 * port's main loop calls port_audio_boot_complete() once boot is done. */
static SDL_AudioDeviceID sEmDevice = 0;
static bool sEmBootDone = false;
static bool sEmUnpaused = false;
static void emMaybeUnpause() {
    if (sEmBootDone && !sEmUnpaused && sEmDevice != 0) {
        SDL_PauseAudioDevice(sEmDevice, 0);
        sEmUnpaused = true;
    }
}
extern "C" void port_audio_boot_complete(void) {
    sEmBootDone = true;
    emMaybeUnpause();
}
#endif

void SDLAudioPlayer::DoPlay(const uint8_t* buf, size_t len) {
#ifdef __EMSCRIPTEN__
    sEmDevice = mDevice;
    emMaybeUnpause();
#endif
    if (Buffered() < 6000) {
        // Don't fill the audio buffer too much in case this happens
        SDL_QueueAudio(mDevice, buf, len);
    }
}
} // namespace Ship
