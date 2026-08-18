# MusicPI developer guide

Read [AGENTS.md](AGENTS.md) first. It contains the authoritative repository,
style, test, and Widget lifecycle rules. This file is a compact orientation
guide for development tools that automatically look for `CLAUDE.md`.

## Commands

Run from the repository root:

```bash
./mpi build headless
./mpi test
./mpi run
```

The desktop target is deprecated. Useful variants:

```bash
build/maschinepi_tests_artefacts/Release/maschinepi_tests \
  --gtest_filter='InputManagerTest.*'
./mpi emulator run --no-build
./mpi emulator qa
./mpi emulator test
./mpi gen-img --help
./mpi deploy --help
```

Do not bypass `./mpi` with ad-hoc CMake commands in normal development; the
CLI owns the supported configure/build/test paths.

## Architecture at a glance

```text
Controller backends (USB MK3 + TCP emulator)
                    |
             ControllerHost
                    |
 Input handlers -> InputManager -> WindowManager / global actions
                                  |
                    Widget-based dual-panel UI
                                  |
                       AudioEngine facade
                                  |
                        Tracktion Engine Edit
```

Current UI code uses `Widget` and `WindowManager`. `ScreenManager`,
`ScreenFactory`, and the old view hierarchy were removed. A small legacy
`ScreenView` remains only for `OptionDialogComponent`; do not use it for new
features.

## Important contracts

- Constructors do not access injected state or start timers.
- `WindowManager` injects hardware, engine, controller, and manager pointers.
- Widgets declare controls in `getOptions(page)` and `getKnobs(page)`.
- Widgets declare all hardware ownership in `requiredResources(page)`.
- Start/stop lifecycle work in `onActivated()` / `onDeactivated()`.
- Prefer the shared 60 Hz `onUiHostTick()` over private UI timers.
- Route input through `handlePad`, `handleButton`, `handleKnob`, and
  `handleTouchstrip`; WindowManager owns knob-touch state.
- Mutations that users should undo go through `juce::UndoableAction` commands.
- Keep Tracktion/JUCE work off controller polling and real-time audio threads.

## State and audio

`AudioEngine` owns the active Tracktion `Edit`, shared undo manager, and the
root application `ValueTree`. `SamplerInstrument` maps 16 pads to Tracktion
audio tracks. Each pad has its own MIDI clip, instrument/sampler plugin,
volume/pan, and level meter. Non-active patterns live in `PadPatternBank` clip
slots; track mode materialises song blocks onto each pad timeline.

Project files use the `.mpi` workflow exposed by `AudioEngine`. Do not add a
second persistence mechanism when state belongs in the existing project tree.

## Tests

Tests live under `tests/` and are discovered by GoogleTest/CTest. Use:

```bash
./mpi test
build/maschinepi_tests_artefacts/Release/maschinepi_tests \
  --gtest_filter='WindowManagerTest.*'
```

Use `JuceHarness`, `MockControllerHost`, and `EngineHarness` for UI,
controller, and Tracktion-facing tests. See
[tests/harness/README.md](tests/harness/README.md).

## More detail

- [Documentation index](docs/README.md)
- [Architecture](docs/architecture.md)
- [UI system](docs/ui-system.md)
- [Audio engine](docs/audio-engine.md)
- [Input system](docs/input-system.md)
