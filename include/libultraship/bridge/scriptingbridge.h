#pragma once

/* C-callable bridge for the ScriptLoader subsystem so TCC-compiled mods
 * can resolve symbols in other loaded mods. The host engine defines and
 * exports these; mod code (TCC defines __DLL__) imports them.
 *
 * ScriptGetFunction(modName, fnName) returns an opaque function pointer
 * to a symbol the named mod's MOD_INIT registered via plain extern, or
 * any HM_API-tagged function in the mod. Returns nullptr if the mod
 * isn't loaded or the symbol isn't found in its temp DLL. */

#if defined(_WIN32)
#  if defined(__DLL__)
#    define LUS_SCRIPT_API __declspec(dllimport)
#  else
#    define LUS_SCRIPT_API __declspec(dllexport)
#  endif
#else
#  define LUS_SCRIPT_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern LUS_SCRIPT_API void* ScriptGetFunction(const char* module, const char* function);

#ifdef __cplusplus
}
#endif
