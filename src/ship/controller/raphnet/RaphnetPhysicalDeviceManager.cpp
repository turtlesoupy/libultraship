#include "ship/controller/raphnet/RaphnetPhysicalDeviceManager.h"

#include <hidapi.h>
#include <spdlog/spdlog.h>

#include <algorithm>

#include "ship/controller/raphnet/RaphnetAdapterEnumerator.h"

namespace Ship {

namespace {

// Local pairing for the deterministic-by-serial sort. Holds a pointer to the
// owning transport plus the channel index reporting CTL_TYPE_N64. We sort by
// (serial, channel) so reboot ordering is plug-independent.
struct N64Candidate {
    std::shared_ptr<RaphnetTransport> Transport;
    uint8_t                           Channel;
};

std::string Wide2Ascii(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) {
        out.push_back((c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?');
    }
    return out;
}

} // namespace

RaphnetPhysicalDeviceManager::RaphnetPhysicalDeviceManager() : mInitialized(false) {
}

RaphnetPhysicalDeviceManager::~RaphnetPhysicalDeviceManager() {
    Shutdown();
}

bool RaphnetPhysicalDeviceManager::Init() {
    if (mInitialized) {
        SPDLOG_INFO("[raphnet] Init called again; rescanning");
        Shutdown();
    }

    SPDLOG_INFO("[raphnet] Init: hid_init()");
    if (hid_init() != 0) {
        // hid_init returns -1 on failure on every backend.
        SPDLOG_ERROR("[raphnet] hid_init() failed; native Raphnet support disabled this session");
        return false;
    }

    auto adapters = RaphnetAdapterEnumerator::EnumerateAdapters();
    if (adapters.empty()) {
        SPDLOG_INFO("[raphnet] no adapters found; SDL HID joystick path will handle any plain joysticks");
        mInitialized = true;
        return true;
    }

    // Open each adapter and remember the open transport. Probing channels
    // happens after the open loop so we can log the full enumerate→open
    // trace before any per-channel queries spam the log.
    for (auto& info : adapters) {
        auto transport = std::make_shared<RaphnetTransport>();
        if (!transport->Open(info.HidPath, info.Vid, info.Pid, info.Serial)) {
            SPDLOG_WARN("[raphnet] failed to open '{}' (vid=0x{:04x} pid=0x{:04x}); skipping",
                        Wide2Ascii(info.Serial), info.Vid, info.Pid);
            continue;
        }
        mTransports.push_back(transport);
        mClaimedVids.insert(info.Vid);
    }

    if (mTransports.empty()) {
        SPDLOG_WARN("[raphnet] {} adapter(s) enumerated but none opened successfully", adapters.size());
        mInitialized = true;
        return true;
    }

    SPDLOG_INFO("[raphnet] {} adapter(s) opened; probing channels for CTL_TYPE_N64", mTransports.size());

    // Probe each channel of each open transport for an N64 controller.
    std::vector<N64Candidate> candidates;
    for (auto& transport : mTransports) {
        for (uint8_t ch = 0; ch < gRntMaxChannelsPerAdapter; ++ch) {
            uint8_t type = gRntCtlTypeNone;
            if (!transport->GetControllerType(ch, type)) {
                // Older firmware (< v3.4) doesn't implement GET_CONTROLLER_TYPE.
                // We can't tell if a controller is plugged in, so fall back to
                // optimistic claim: assume channel 0 has an N64 pad and skip the
                // rest. v3.0..v3.3 firmware is rare; rocking back to RAW_SI
                // probe (issuing N64_GET_CAPABILITIES) is the proper fallback
                // but adds protocol surface. Defer that to a follow-up.
                if (ch == 0) {
                    SPDLOG_WARN("[raphnet] adapter '{}' GET_CONTROLLER_TYPE not supported by firmware "
                                "(version='{}'); claiming channel 0 optimistically",
                                Wide2Ascii(transport->GetSerial()), transport->GetVersionString());
                    candidates.push_back({ transport, 0 });
                }
                break;
            }
            if (type == gRntCtlTypeN64) {
                candidates.push_back({ transport, ch });
            } else if (type == gRntCtlTypeNone) {
                // Empty channel — log but don't claim.
                SPDLOG_DEBUG("[raphnet] adapter '{}' chn={}: empty",
                             Wide2Ascii(transport->GetSerial()), ch);
            } else {
                SPDLOG_INFO("[raphnet] adapter '{}' chn={}: non-N64 controller type 0x{:02x}; "
                            "this is SSB64 — channel skipped",
                            Wide2Ascii(transport->GetSerial()), ch, type);
            }
        }
    }

    // Sort candidates deterministically by (serial, channel) so port
    // assignment is reboot-stable.
    std::sort(candidates.begin(), candidates.end(),
              [](const N64Candidate& a, const N64Candidate& b) {
                  const auto& sa = a.Transport->GetSerial();
                  const auto& sb = b.Transport->GetSerial();
                  if (sa != sb) {
                      return sa < sb;
                  }
                  return a.Channel < b.Channel;
              });

    // Bind to game ports in order, up to MAXCONTROLLERS.
    int portIdx = 0;
    for (auto& cand : candidates) {
        if (portIdx >= MAXCONTROLLERS) {
            SPDLOG_WARN("[raphnet] more N64 candidates ({}) than game ports ({}); ignoring extras",
                        candidates.size(), MAXCONTROLLERS);
            break;
        }
        mPortBindings[portIdx].Transport = cand.Transport;
        mPortBindings[portIdx].Channel = cand.Channel;
        SPDLOG_INFO("[raphnet] port {} ← adapter serial='{}' chn={} (vid=0x{:04x} pid=0x{:04x})",
                    portIdx, Wide2Ascii(cand.Transport->GetSerial()), cand.Channel,
                    cand.Transport->GetVid(), cand.Transport->GetPid());
        ++portIdx;
    }

    if (portIdx == 0) {
        SPDLOG_INFO("[raphnet] no N64 controllers detected on any open adapter; "
                    "all game ports remain unbound (SDL still handles plain joysticks)");
    }

    mInitialized = true;
    return true;
}

void RaphnetPhysicalDeviceManager::Shutdown() {
    if (!mInitialized && mTransports.empty()) {
        return;
    }

    SPDLOG_INFO("[raphnet] Shutdown: closing {} transport(s)", mTransports.size());

    for (auto& binding : mPortBindings) {
        binding.Transport.reset();
        binding.Channel = 0;
    }

    // Defensive belt-and-suspenders: turn off vibration on every claimed
    // (port, channel) before closing so a long-running rumble effect can't
    // outlive the process. RaphnetTransport::Close() also calls SUSPEND_-
    // POLLING(0) which restores HID joystick mode.
    for (auto& transport : mTransports) {
        if (transport == nullptr) {
            continue;
        }
        for (uint8_t ch = 0; ch < gRntMaxChannelsPerAdapter; ++ch) {
            transport->SetVibration(ch, false);
        }
        transport->Close();
    }
    mTransports.clear();
    mClaimedVids.clear();

    if (hid_exit() != 0) {
        SPDLOG_WARN("[raphnet] hid_exit() returned non-zero");
    } else {
        SPDLOG_INFO("[raphnet] hid_exit() OK");
    }

    mInitialized = false;
}

std::shared_ptr<RaphnetTransport>
RaphnetPhysicalDeviceManager::GetTransportForPort(uint8_t portIndex) const {
    if (portIndex >= MAXCONTROLLERS) {
        return nullptr;
    }
    return mPortBindings[portIndex].Transport;
}

int RaphnetPhysicalDeviceManager::GetChannelForPort(uint8_t portIndex) const {
    if (portIndex >= MAXCONTROLLERS) {
        return -1;
    }
    return mPortBindings[portIndex].Transport != nullptr ? mPortBindings[portIndex].Channel : -1;
}

bool RaphnetPhysicalDeviceManager::IsPortClaimed(uint8_t portIndex) const {
    if (portIndex >= MAXCONTROLLERS) {
        return false;
    }
    return mPortBindings[portIndex].Transport != nullptr;
}

int RaphnetPhysicalDeviceManager::ClaimedPortCount() const {
    int n = 0;
    for (const auto& b : mPortBindings) {
        if (b.Transport != nullptr) {
            ++n;
        }
    }
    return n;
}

bool RaphnetPhysicalDeviceManager::IsClaimedSdlVendor(uint16_t vid) const {
    return mClaimedVids.contains(vid);
}

} // namespace Ship
