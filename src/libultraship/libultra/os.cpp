#include "libultraship/libultraship.h"
#include <SDL2/SDL.h>
#include <ratio>

// Establish a chrono duration for the N64 46.875MHz clock rate
typedef std::ratio<3000, 64> n64ClockRatio;
typedef std::ratio_divide<std::micro, n64ClockRatio> n64CycleRate;
typedef std::chrono::duration<long long, n64CycleRate> n64CycleRateDuration;

extern "C" {
uint8_t __osMaxControllers = MAXCONTROLLERS;
uint64_t __osCurrentTime = 0;

int32_t osContInit(OSMesgQueue* mq, uint8_t* controllerBits, OSContStatus* status) {
    *controllerBits = 0;
    status->status |= 1;

    std::string controllerDb = Ship::Context::LocateFileAcrossAppDirs("gamecontrollerdb.txt");
    int mappingsAdded = SDL_GameControllerAddMappingsFromFile(controllerDb.c_str());
    if (mappingsAdded >= 0) {
        SPDLOG_INFO("Added SDL game controllers from \"{}\" ({})", controllerDb, mappingsAdded);
    } else {
        SPDLOG_ERROR("Failed add SDL game controller mappings from \"{}\" ({})", controllerDb, SDL_GetError());
    }

    // Run RaphnetPhysicalDeviceManager init BEFORE SDL_Init(GAMECONTROLLER).
    // The raphnet adapter exposes both a HID joystick interface (which SDL
    // would grab) and a vendor command interface (which we drive via
    // hidapi). On Windows, DirectInput tends to grab whichever is opened
    // first and refuses sharing — claiming via hidapi first guarantees we
    // win the race. Internally PreInitRaphnet also globally skip-lists the
    // claimed VIDs so the SDL refresh below ignores them.
    Ship::Context::GetInstance()->GetControlDeck()->PreInitRaphnet();

    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        SPDLOG_ERROR("Failed to initialize SDL game controllers ({})", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    Ship::Context::GetInstance()->GetControlDeck()->Init(controllerBits);

    return 0;
}

int32_t osContStartReadData(OSMesgQueue* mesg) {
    if (mesg != nullptr) {
        osSendMesg(mesg, OSMesg{}, OS_MESG_NOBLOCK);
    }
    return 0;
}

void osContGetReadData(OSContPad* pad) {
    /* `OSContPad` means two different layouts in the same build: the
     * LUS struct (0x24 bytes, with gyro/right-stick for OOT/MM) and the
     * SSB64 decomp's PR/os.h struct (6 bytes: u16 button, s8 stick_x,
     * s8 stick_y, u8 errno). The caller hands us a pointer to the
     * game-sized array but this TU sees the LUS layout, so memsetting
     * or writing sizeof(OSContPad) * N overruns into the next BSS
     * symbol. In SSB64 that neighbor is `sSYControllerDescs[]`, whose
     * leading `unk00` is the prior-frame button state used for edge
     * detection — zeroing it every Read made a held button re-fire an
     * edge every frame, which presented as 2–3x menu input.
     *
     * Write into a local LUS-layout buffer, then copy only the fields
     * the game's struct actually has. */
    struct GameContPad {
        uint16_t button;
        int8_t   stick_x;
        int8_t   stick_y;
        uint8_t  err_no;
    };

    OSContPad lus_pads[MAXCONTROLLERS] = {};
    Ship::Context::GetInstance()->GetControlDeck()->WriteToPad(lus_pads);

    GameContPad* out = reinterpret_cast<GameContPad*>(pad);
    for (int i = 0; i < __osMaxControllers; i++) {
        out[i].button  = static_cast<uint16_t>(lus_pads[i].button);
        out[i].stick_x = lus_pads[i].stick_x;
        out[i].stick_y = lus_pads[i].stick_y;
        out[i].err_no  = lus_pads[i].err_no;
    }
}

void osSetTime(OSTime time) {
    __osCurrentTime =
        std::chrono::duration_cast<n64CycleRateDuration>(std::chrono::steady_clock::now().time_since_epoch()).count() +
        time;
}

// Returns the OS time matching the N64 46.875MHz cycle rate
uint64_t osGetTime() {
    return std::chrono::duration_cast<n64CycleRateDuration>(std::chrono::steady_clock::now().time_since_epoch())
               .count() -
           __osCurrentTime;
}

// Returns the CPU clock count matching the N64 46.875Mhz cycle rate
uint32_t osGetCount() {
    return std::chrono::duration_cast<n64CycleRateDuration>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

OSPiHandle* osCartRomInit() {
    return NULL;
}

int osSetTimer(OSTimer* t, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    return 0;
}

int32_t osEPiStartDma(OSPiHandle* pihandle, OSIoMesg* mb, int32_t direction) {
    return 0;
}

uint32_t osAiGetLength() {
    // TODO: Implement
    return 0;
}

int32_t osAiSetNextBuffer(void* buff, size_t len) {
    // TODO: Implement
    return 0;
}

int32_t __osMotorAccess(OSPfs* pfs, uint32_t vibrate) {
    auto io = Ship::Context::GetInstance()->GetControlDeck()->GetControllerByPort(pfs->channel)->GetRumble();
    if (vibrate) {
        io->StartRumble();
    } else {
        io->StopRumble();
    }

    return 0;
}

int32_t osMotorInit(OSMesgQueue* ctrlrqueue, OSPfs* pfs, int32_t channel) {
    pfs->channel = channel;
    return 0;
}
}
