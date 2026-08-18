# Architecture

MusicPI is a headless JUCE application built around a Tracktion Engine
`Edit`, a normalised input dispatcher, and a two-panel Widget UI. The physical
MK3 and TCP emulator are peer controller backends.

## Layers

```text
┌──────────────────────────────────────────────────────────────┐
│ Application                                                   │
│ App, MainWindow/headless host, UiHost, DataPaths              │
├──────────────────────────────────────────────────────────────┤
│ UI                                                            │
│ WindowManager, Widget subclasses, dialogs, components, theme  │
│ HardwareState owns LEDs/buttons/knobs/pads by resource ID      │
├──────────────────────────────────────────────────────────────┤
│ Input and control                                             │
│ InputManager, input handlers, ControllerHost, IController     │
├──────────────────────────────────────────────────────────────┤
│ Domain                                                        │
│ AudioEngine, SamplerInstrument, GroupManager, commands,       │
│ KeyboardInstrumentBank, discovery, sample index/taxonomy      │
├──────────────────────────────────────────────────────────────┤
│ Platform and third party                                     │
│ Tracktion Engine, JUCE, libusb MK3 driver, TCP emulator        │
└──────────────────────────────────────────────────────────────┘
```

Dependencies point downward. Widgets use `AudioEngine` and commands; engine
code does not depend on widgets. Controller backends emit the same event
types, so application logic does not distinguish physical from emulated input.

## Startup and ownership

`App` creates the JUCE/Tracktion environment. `UiHost` owns the major runtime
objects and wires them together:

```text
UiHost
├── AudioEngine
│   ├── Tracktion Edit
│   ├── SamplerInstrument
│   ├── GroupManager
│   ├── KeyboardInstrumentBank
│   ├── Arpeggiator
│   └── T9Dictionary
├── InputManager
├── ControllerHost
│   ├── Mk3Controller (USB)
│   └── EmulatorController (TCP, when available)
└── WindowManager
    ├── left/right Widget slots
    ├── modal dialog stack
    ├── system Widgets
    └── HardwareState
```

`WindowManager::open()` injects `HardwareState`, `AudioEngine`,
`ControllerHost`, and `WindowManager` into each Widget before activation.

## Core flows

### Hardware input

```text
MK3 HID or emulator TCP
  -> IController implementation
  -> ControllerHost
  -> ControllerInputHandler / InputManager
  -> global handler or WindowManager
  -> focused Widget/dialog
  -> AudioEngine command/state change
```

Option buttons and encoders are panel-aware: d1-d4/k1-k4 target the left
panel; d5-d8/k5-k8 target the right. Pads go to the top dialog or focused
Widget. System Widgets handle global transport/group behaviour without
occupying a display slot.

### Audio and sequencing

```text
Widget or input action
  -> UndoableAction or AudioEngine/SamplerInstrument method
  -> Tracktion Edit ValueTree/plugin/clip mutation
  -> Tracktion audio graph
  -> audio device
```

Each sampler pad owns a Tracktion audio track. A per-pad MIDI clip drives its
sampler/instrument plugin. Pattern slots are kept in `PadPatternBank`; song
mode materialises arranger blocks as MIDI clips on the pad timelines.

### Display and LEDs

`UiHost` runs a shared 60 Hz UI heartbeat. It advances active Widgets,
refreshes bars, renders dirty 480x272 panel images, and pushes display frames
through `ControllerHost`. `HardwareState` batches LED changes and flushes only
dirty resources.

## UI lifecycle

```text
construct
  -> dependency injection
  -> requiredResources(page) claims
  -> onActivated(panelOffset)
  -> input / onUiHostTick / paintPage
  -> onDeactivated()
  -> releaseAll(widgetId)
  -> destroy or stash for later restoration
```

Constructors must not access injected dependencies. Lifecycle methods own
handler registration, background work, and cleanup.

## State and persistence

`AudioEngine` owns the application `ValueTree` children for transport, pads,
groups, mixer, UI, and settings alongside the Tracktion `Edit`. User-visible
mutations use the Edit's shared `juce::UndoManager`.

`.mpi` project save/load is the application-level persistence path. It stores
the Tracktion edit plus MusicPI state such as pad metadata, groups, UI
settings and T9 data. Sample paths are resolved through
`AudioEngine::resolveSampleFileForPlayback()` when projects move.

## Thread model

- **Audio thread:** Tracktion graph rendering; never block, allocate
  needlessly, perform I/O, or touch JUCE Components.
- **Message thread:** Widget lifecycle, input actions, ValueTree/UI mutation,
  rendering, and callback completion.
- **Controller threads:** USB/TCP polling and decoding; dispatch toward the
  message-thread-facing layers.
- **Worker pools:** sample indexing and other blocking work; UI callbacks
  return through `MessageManager::callAsync`.

Use safe/weak component references for asynchronous UI completion and cancel
workers during deactivation/destruction.

## Repository boundaries

- `src/engine/commands/` contains undoable mutations.
- `src/ui/widget/` contains all new top-level views and dialogs.
- `src/ui/components/` contains reusable child components.
- `src/ui/screen/` is legacy compatibility for `OptionDialogComponent`, not a
  foundation for new work.
- `src/discovery/` abstracts metadata search and media acquisition.
- `src/samples/` owns filesystem indexing and classification.

See [UI system](ui-system.md), [Audio engine](audio-engine.md), and
[Input system](input-system.md) for subsystem details.
