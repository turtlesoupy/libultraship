#include "libultraship/bridge/scriptingbridge.h"

#ifndef DISABLE_SCRIPTING

#include "ship/Context.h"
#include "ship/scripting/ScriptLoader.h"

extern "C" {

void* ScriptGetFunction(const char* module, const char* function) {
    if (module == nullptr || function == nullptr) {
        return nullptr;
    }
    auto loader = Ship::Context::GetInstance()->GetScriptLoader();
    if (!loader) {
        return nullptr;
    }
    return loader->GetFunction(std::string(module), std::string(function));
}

} // extern "C"

#endif // DISABLE_SCRIPTING
