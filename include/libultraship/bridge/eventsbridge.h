#pragma once

#include "stdint.h"
#include "ship/events/EventTypes.h"

/* libultraship is a STATIC library; symbols defined inside it are only
 * pulled into a host EXE if explicitly referenced. Mods (compiled by TCC
 * at runtime) reference these bridge functions, but the EXE itself does
 * not — the linker would otherwise drop them and they'd be missing from
 * the EXE's export table.
 *
 * LUS_BRIDGE_API forces export from the host EXE and import into mods.
 * On the engine-side definition (`extern "C" LUS_BRIDGE_API ...`),
 * dllexport tells the linker to emit the symbol in the EXE's .edata.
 * On the mod-side declaration (TCC defines __DLL__), dllimport tells
 * TCC to look up the symbol via the EXE's import table. */
#if defined(_WIN32)
#  if defined(__DLL__)
#    define LUS_BRIDGE_API __declspec(dllimport)
#  else
#    define LUS_BRIDGE_API __declspec(dllexport)
#  endif
#else
#  define LUS_BRIDGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern LUS_BRIDGE_API EventID EventSystemRegisterEvent(const char* name);
extern LUS_BRIDGE_API ListenerID EventSystemRegisterListener(EventID id, EventCallback callback, EventPriority priority,
                                                             const char* file, int line);
extern LUS_BRIDGE_API void EventSystemUnregisterListener(EventID ev, ListenerID id);
extern LUS_BRIDGE_API void EventSystemCallEvent(EventID id, void* event, const char* file, int line, const char* key);

#ifdef __cplusplus
}
#endif
