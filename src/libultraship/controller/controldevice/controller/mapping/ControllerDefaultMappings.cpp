#include "libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"
#include "libultraship/libultra/controller.h"

namespace LUS {
ControllerDefaultMappings::ControllerDefaultMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>> defaultKeyboardKeyToButtonMappings,
    std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, Ship::KbScancode>>>
        defaultKeyboardKeyToAxisDirectionMappings,
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GameControllerButton>>
        defaultSDLButtonToButtonMappings,
    std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GameControllerButton>>>
        defaultSDLButtonToAxisDirectionMappings,
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GameControllerAxis, int32_t>>>
        defaultSDLAxisDirectionToButtonMappings,
    std::unordered_map<Ship::StickIndex,
                       std::vector<std::pair<Ship::Direction, std::pair<SDL_GameControllerAxis, int32_t>>>>
        defaultSDLAxisDirectionToAxisDirectionMappings)
    : Ship::ControllerDefaultMappings(defaultKeyboardKeyToButtonMappings, defaultKeyboardKeyToAxisDirectionMappings,
                                      defaultSDLButtonToButtonMappings, defaultSDLButtonToAxisDirectionMappings,
                                      defaultSDLAxisDirectionToButtonMappings,
                                      defaultSDLAxisDirectionToAxisDirectionMappings) {
    SetDefaultKeyboardKeyToButtonMappings(defaultKeyboardKeyToButtonMappings);
    SetDefaultSDLButtonToButtonMappings(defaultSDLButtonToButtonMappings);
    SetDefaultSDLAxisDirectionToButtonMappings(defaultSDLAxisDirectionToButtonMappings);
}

ControllerDefaultMappings::ControllerDefaultMappings()
    : ControllerDefaultMappings(
          std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>>(),
          std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, Ship::KbScancode>>>(),
          std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GameControllerButton>>(),
          std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GameControllerButton>>>(),
          std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GameControllerAxis, int32_t>>>(),
          std::unordered_map<Ship::StickIndex,
                             std::vector<std::pair<Ship::Direction, std::pair<SDL_GameControllerAxis, int32_t>>>>()) {
}

ControllerDefaultMappings::~ControllerDefaultMappings() {
}

void ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>> defaultKeyboardKeyToButtonMappings) {
    if (!defaultKeyboardKeyToButtonMappings.empty()) {
        Ship::ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings(defaultKeyboardKeyToButtonMappings);
        return;
    }

    Ship::ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings({
        /* Right-hand cluster mirrors the N64 pad under the fingers:
         * J=A, K=B, L=Z, I=L, O=R (WASD stick, Space=Start, arrows=C).
         * The web launcher's controls tutorial teaches exactly this. */
        { BTN_A, { Ship::KbScancode::LUS_KB_J } },
        { BTN_B, { Ship::KbScancode::LUS_KB_K } },
        { BTN_L, { Ship::KbScancode::LUS_KB_I } },
        { BTN_R, { Ship::KbScancode::LUS_KB_O } },
        { BTN_Z, { Ship::KbScancode::LUS_KB_L } },
        { BTN_START, { Ship::KbScancode::LUS_KB_SPACE } },
        { BTN_CUP, { Ship::KbScancode::LUS_KB_ARROWKEY_UP } },
        { BTN_CDOWN, { Ship::KbScancode::LUS_KB_ARROWKEY_DOWN } },
        { BTN_CLEFT, { Ship::KbScancode::LUS_KB_ARROWKEY_LEFT } },
        { BTN_CRIGHT, { Ship::KbScancode::LUS_KB_ARROWKEY_RIGHT } },
        { BTN_DUP, { Ship::KbScancode::LUS_KB_T } },
        { BTN_DDOWN, { Ship::KbScancode::LUS_KB_G } },
        { BTN_DLEFT, { Ship::KbScancode::LUS_KB_F } },
        { BTN_DRIGHT, { Ship::KbScancode::LUS_KB_H } },
    });
}

void ControllerDefaultMappings::SetDefaultSDLButtonToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GameControllerButton>>
        defaultSDLButtonToButtonMappings) {
    if (!defaultSDLButtonToButtonMappings.empty()) {
        Ship::ControllerDefaultMappings::SetDefaultSDLButtonToButtonMappings(defaultSDLButtonToButtonMappings);
        return;
    }

    Ship::ControllerDefaultMappings::SetDefaultSDLButtonToButtonMappings({
        { BTN_A, { SDL_CONTROLLER_BUTTON_A } },
        { BTN_B, { SDL_CONTROLLER_BUTTON_B } },
        { BTN_L, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER } },
        { BTN_START, { SDL_CONTROLLER_BUTTON_START } },
        { BTN_DUP, { SDL_CONTROLLER_BUTTON_DPAD_UP } },
        { BTN_DDOWN, { SDL_CONTROLLER_BUTTON_DPAD_DOWN } },
        { BTN_DLEFT, { SDL_CONTROLLER_BUTTON_DPAD_LEFT } },
        { BTN_DRIGHT, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT } },
    });
}

void ControllerDefaultMappings::SetDefaultSDLAxisDirectionToButtonMappings(
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GameControllerAxis, int32_t>>>
        defaultSDLAxisDirectionToButtonMappings) {
    if (!defaultSDLAxisDirectionToButtonMappings.empty()) {
        Ship::ControllerDefaultMappings::SetDefaultSDLAxisDirectionToButtonMappings(
            defaultSDLAxisDirectionToButtonMappings);
        return;
    }

    Ship::ControllerDefaultMappings::SetDefaultSDLAxisDirectionToButtonMappings({
        { BTN_R, { { SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1 } } },
        { BTN_Z, { { SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1 } } },
        { BTN_CUP, { { SDL_CONTROLLER_AXIS_RIGHTY, -1 } } },
        { BTN_CDOWN, { { SDL_CONTROLLER_AXIS_RIGHTY, 1 } } },
        { BTN_CLEFT, { { SDL_CONTROLLER_AXIS_RIGHTX, -1 } } },
        { BTN_CRIGHT, { { SDL_CONTROLLER_AXIS_RIGHTX, 1 } } },
    });
}
} // namespace LUS
