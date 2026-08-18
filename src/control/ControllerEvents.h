#pragma once

// Standalone POD event types produced by IController implementations and
// consumed by ControllerHost / Widget / InputManager. Extracted from
// ControllerHost.h so downstream headers (notably src/ui/widget/Widget.h) can
// see the struct layout without dragging in ControllerHost's transitive
// includes (HardwareState, IController, std::function, std::unordered_map, ...)
// into every widget translation unit.
//
// ControllerHost re-exports these as nested aliases (`ControllerHost::PadEvent`
// etc.) so existing callers keep their spelling.

#include <cstdint>
#include <string>

namespace controller_events
{
    struct ButtonEvent {
        std::string name;
        bool pressed{};
        bool shift{};
    };

    struct PadEvent {
        uint8_t pad{};
        bool pressed{};
        uint16_t pressure{};
        bool shift{};
        bool macro{};
    };

    struct KnobEvent {
        std::string name;
        int16_t delta{};
        uint16_t absolute{};
        bool shift{};
    };

    struct StepperEvent {
        int8_t direction{};
        uint8_t position{};
        bool shift{};
    };

    struct TouchstripEvent {
        uint8_t finger{};
        bool touching{};
        uint16_t position{};
        bool shift{};
    };
}
