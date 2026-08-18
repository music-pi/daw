# UI guidelines

The top-level unit is a **Widget**, placed by **WindowManager** on a physical
left/right display panel. Avoid the removed ScreenManager/View terminology in
new code and documentation.

## Hardware surface

- Two 480x272 panels, 960x272 combined.
- Four option slots and four knob slots per panel.
- d1-d4/k1-k4 belong to the left; d5-d8/k5-k8 to the right.
- The 16 logical pads map to the physical 4x4 grid through the controller
  layer; do not invent a separate LED order.
- Use `UiTheme` dimensions, fonts, and colours. Keep text brief and readable
  at hardware resolution.

## Placement

Declare placement with `DisplayConstraint` in `describe()`:

- required side constraints win;
- `Any` may open on either panel;
- `Both` spans the two displays and maps page 0 left/page 1 right.

Open through `WindowManager::open()`. Use `takeWidget()` when a temporary flow
must restore the previous occupant. Use `showDialog()` for modal Widgets.

## Controls and ownership

- Return options and knobs from `getOptions(page)` / `getKnobs(page)`.
- Declare every button, encoder, pad, and navigation resource in
  `requiredResources(page)`.
- Write LEDs only while the Widget owns the resource in `HardwareState`.
- Do not implement private knob-touch tracking; WindowManager owns it.
- Use `isInputActive` for touch-dependent display state.

## Lifecycle and refresh

- Constructors are state-free.
- Register/cancel work in `onActivated()` / `onDeactivated()`.
- Prefer `onUiHostTick()` for animation.
- `Widget::repaint()` marks the hardware surface dirty; leaf components must
  call `requestUiRefresh()` when repainting outside a Widget-typed path.
- Never mutate JUCE Components from controller, worker, or audio threads.

See [UI system](../ui-system.md) for the complete contract.
