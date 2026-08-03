#pragma once

#include <cstdint>

namespace makxd {

    enum class ApiProtocol : uint8_t {
        KM = 0,
        MAK_API = 1,
    };

    enum class ConnectionMethod : uint8_t {
        COM = 0,
        UDP = 1,
        BLE = 2,
    };

    enum class UdpWireMode : uint8_t {
        HOST = 0,
        RAW = 1,
    };

    enum class ApiVerb : uint8_t {
        GET = 0x00,
        SET = 0x01,
        EXEC = 0x02,
    };

    enum class ApiOpcode : uint8_t {
        DEVICE = 0x02,
        BUTTONS = 0x10,
        LEFT = 0x11,
        RIGHT = 0x12,
        MIDDLE = 0x13,
        SIDE1 = 0x14,
        SIDE2 = 0x15,
        MOVE_MASK = 0x16,
        WHEEL_MASK = 0x17,
        MOVE = 0x18,
        WHEEL = 0x19,
        LEFT_MASK = 0x1A,
        RIGHT_MASK = 0x1B,
        MIDDLE_MASK = 0x1C,
        SIDE1_MASK = 0x1D,
        SIDE2_MASK = 0x1E,
        KEY_DOWN = 0x20,
        KEY_UP = 0x21,
        KEY_INIT = 0x22,
        KEY_PRESS = 0x23,
        KEY_STRING = 0x24,
        KEY_IS_DOWN = 0x25,
        KEY_MULTI_DOWN = 0x26,
        KEY_MULTI_UP = 0x27,
        KEY_MULTI_PRESS = 0x28,
        KEY_MASK = 0x29,
        KEY_REMAP = 0x2A,
        KEY_KEYS = 0x2B,
        CONTROLLER_STATE = 0x40,
        CONTROLLER_CONTROL = 0x41,
        CONTROLLER_MASK = 0x51,
    };

    struct DeviceRoute {
        uint8_t routeMask{};
        uint16_t mouseUframes{};
        uint16_t keyboardUframes{};
        uint16_t controllerUframes{};
        uint32_t generation{};
        uint8_t controllerFamily{};
        uint8_t controllerProtocol{};
        uint8_t controllerLayout{};
        uint32_t controllerSupportedLow{};
        uint32_t controllerSupportedHigh{};

        [[nodiscard]] bool hasMouse() const noexcept { return (routeMask & 0x01u) != 0u; }
        [[nodiscard]] bool hasKeyboard() const noexcept { return (routeMask & 0x02u) != 0u; }
        [[nodiscard]] bool hasController() const noexcept { return (routeMask & 0x04u) != 0u; }
        [[nodiscard]] bool controllerSupports(uint8_t control) const noexcept {
            return control < 32u
                ? (controllerSupportedLow & (uint32_t{1} << control)) != 0u
                : control < 55u &&
                    (controllerSupportedHigh & (uint32_t{1} << (control - 32u))) != 0u;
        }
        [[nodiscard]] double mouseHz() const noexcept {
            return mouseUframes == 0u ? 0.0 : 8000.0 / mouseUframes;
        }
        [[nodiscard]] double keyboardHz() const noexcept {
            return keyboardUframes == 0u ? 0.0 : 8000.0 / keyboardUframes;
        }
        [[nodiscard]] double controllerHz() const noexcept {
            return controllerUframes == 0u ? 0.0 : 8000.0 / controllerUframes;
        }
    };

} // namespace makxd
