#include "libultraship/bridge/eventsbridge.h"
#include "ship/events/EventSystem.h"
#include "ship/Context.h"

extern "C" {

EventID EventSystemRegisterEvent(const char* name) {
    return Ship::Context::GetInstance()->GetEventSystem()->RegisterEvent(name);
}

ListenerID EventSystemRegisterListener(EventID id, EventCallback callback, EventPriority priority, const char* file,
                                       int line) {
    return Ship::Context::GetInstance()->GetEventSystem()->RegisterListener(id, callback, priority, file, line);
}

void EventSystemUnregisterListener(EventID ev, ListenerID id) {
    Ship::Context::GetInstance()->GetEventSystem()->UnregisterListener(ev, id);
}

void EventSystemCallEvent(EventID id, void* event, const char* file, int line, const char* key) {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx) return;
    auto es = ctx->GetEventSystem();
    if (!es) return;
    es->CallEvent(id, static_cast<IEvent*>(event), file, line, key);
}
}