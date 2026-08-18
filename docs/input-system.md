# Input system

MusicPI normalises MIDI, keyboard, physical MK3, and emulator events before
routing them through priority-ordered handlers and the active Widget tree.

## Flow

```text
MIDI device / JUCE key / USB MK3 / TCP emulator
          -> MidiInputHandler, KeyboardInputHandler,
             ControllerInputHandler or ControllerHost
          -> InputEvent
          -> InputManager
          -> modal/view/component/global action
          -> WindowManager or AudioEngine
```

`ControllerHost` owns both controller backends and fans display/LED output to
every connected controller. USB and emulator input use the shared event
structures in `src/control/ControllerEvents.h`.

## InputEvent

`src/input/InputEvent.h` defines `InputEvent::Type`:

- `Midi`
- `Key`
- `Controller`
- `Pad`
- `Button`
- `Knob`
- `Stepper`
- `Touchstrip`

The event stores source identity, MIDI/key data, and a `NamedValueSet` for
type-specific fields. Use convenience accessors such as `isPressed()`,
`isShift()`, `padIndex()`, and `stepperDirection()`. Handlers set
`event.consumed=true` to stop lower-priority dispatch.

Factory helpers (`makePad`, `makeButton`, `makeKnob`, and others) are the
canonical way to populate metadata.

## InputManager

`src/input/InputManager.h` stores handlers in descending priority:

```cpp
enum class HandlerPriority
{
    Modal = 200,
    View = 100,
    Component = 50,
    Global = 0
};
```

Handlers may have an empty global context or a named context enabled with
`setActiveContext(context, true)`. Register through the typed helpers:

```cpp
auto id = inputManager.addButtonHandler(
    InputManager::HandlerPriority::View,
    "my-widget",
    "sampling",
    [this](InputEvent& event)
    {
        if (!event.isPressed())
            return;
        openSampler();
        event.consumed = true;
    });

inputManager.removeHandler(id);
```

Other helpers cover MIDI, keys, controllers, pads, knobs, the navigation
stepper, and the touch strip. Dispatch snapshots shared handler objects before
invoking callbacks, so handlers may safely unregister during dispatch.

Context activation and handler registration belong in the owning object's
lifecycle. Widgets should normally prefer their direct `handle*` hooks when
`WindowManager` already supplies the desired routing.

## ControllerHost and WindowManager

`ControllerHost`:

- creates `Mk3Controller` and the optional `EmulatorController`;
- tracks Shift/Select and other modifier state;
- converts backend callbacks into `InputManager` events;
- binds global transport behaviour;
- forwards panel controls to `WindowManager`;
- broadcasts display frames and LED writes.

`WindowManager` applies UI-specific routing:

- d1-d4 and k1-k4 target the left panel;
- d5-d8 and k5-k8 target the right panel;
- dialogs receive input before panel Widgets;
- pads and the touch strip target the focused Widget when no dialog is open;
- arrow buttons scroll normal multi-page Widgets or are forwarded when the
  Widget owns them;
- knob touch state is managed centrally.

Do not register custom knob-touch handlers inside Widgets.

## Controller names

Stable names are defined by the MK3 mapping and used throughout tests:

- options: `d1` through `d8`
- encoders: `k1` through `k8`
- groups: `g1` through `g8`
- navigation: `arrowLeft`, `arrowRight`, `navUp`, `navDown`, `navPush`
- transport: `play`, `stop`, `recCountIn`, `restartLoop`
- modes include `padMode`, `step`, `pattern`, `mixer`, `sampling`,
  `arranger`, `keyboard`, and `browserPlugin`

Consult `src/control/HardwareConstants.h`, the MK3 map tests, and existing
`UiHost` registrations before introducing another spelling.

## Threading

Controller polling threads must not mutate JUCE Components. `ControllerHost`
and the input layer transfer work toward the message thread. Keep callbacks
short; network, disk, and analysis work belongs on worker threads.

## Testing

- `InputManagerTests.cpp` covers priority, matching, contexts, consumption,
  and unregister-during-dispatch behaviour.
- MK3 HID/map parity suites cover physical report names and ranges.
- `EmulatorProtocolTests.cpp` covers TCP protocol parity.
- `MockControllerHost` drives Widget-level hardware tests without USB.

Run all tests with `./mpi test`; see [testing notes](agents/testing.md).
