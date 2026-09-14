#include "makxd.h"

// MAKCU requires firmware with zero-state handoff support. See
// protocol/MAK_API.md#makcu-controller-handoff before using older firmware.
// The application owns action completion; this setter has no duration.
bool send_controller_action(makxd::Device& device, int16_t x, int16_t y,
                            uint16_t trigger) {
    makxd::ControllerState state{};
    state.rightStickX = x;
    state.rightStickY = y;
    state.rightTrigger = trigger;
    return device.setControllerState(state);
}

// Call from completion AND cancellation paths while connected. Check the
// result: a failed send has not requested handoff. The firmware handles the
// transition to physical input; returning true is not USB-completion proof.
bool finish_controller_action(makxd::Device& device) {
    return device.setControllerState(makxd::ControllerState{});
}

// If another control is still active, send its current value alongside zero
// for only the completed stick pair / trigger, instead of clearing everything.
