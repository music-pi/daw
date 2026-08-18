# Repository Guidelines

## Project Structure & Module Organization
Source lives in `src/`, grouped by responsibility: `app/` for JUCE scaffolding, `engine/` for Tracktion Engine wrappers (AudioEngine, SamplerInstrument, GroupManager), `input/` for input handling (InputManager, handlers), `control/` for controller abstractions (ControllerHost, IController), `devices/` for hardware drivers (Mk3Device), and `ui/` for the Widget-based UI system. Tests reside in `tests/`. Generated build artefacts go to `build/` by default, while CMake helper modules sit under `cmake/` and third-party sources cache in `external/`. Keep new modules domain-focused and mirror this folder layout so `CMakeLists.txt` additions stay small.

## Build, Test, and Development Commands
Build: `./mpi build headless` (from project root). Tests: `./mpi test`. The `./mpi` CLI handles all build/test/run commands. Always run from the project root directory.

Desktop target is deprecated. Default to headless unless explicitly told otherwise.

## Coding Style & Naming Conventions
Code is modern C++20 with JUCE idioms. Use four-space indentation, Allman braces, and early returns to match existing files. Prefer `auto` only when the type is already obvious. Classes stay in PascalCase (`ControllerHost`), member functions and variables use camelCase, and constants use PascalCase or `k`-prefixed camelCase depending on JUCE expectations. Keep headers self-contained and include headers from `src/` using quotes; system and JUCE headers go after a blank line.

## Testing Guidelines
Tests build into the `maschinepi_tests` console target and are executed through CTest/GTest discovery. Run `./mpi test` to invoke all tests. For filtering, run the binary directly: `build/maschinepi_tests_artefacts/Release/maschinepi_tests --gtest_filter=InputManagerTest.*`. Avoid exporting `GTEST_FILTER` around `./mpi test`, because CTest's PRE_TEST discovery can leave its generated registry narrowed until the test binary is relinked.

All suites live under `tests/` and use GoogleTest fixtures—follow the AAA pattern (arrange, act, assert) and prefer fixtures when JUCE initialisation or temp directories are required.

**Harness recap**
- `tests/harness/JuceHarness.*` boots JUCE in headless mode and provides a `ComponentHost` for mounting widgets without spawning native windows.
- `tests/harness/MockControllerHost.*` subclasses `ControllerHost` with hardware access disabled. It can inject named button events and records button brightness plus display-frame writes. Inspect `WindowManager::getHardwareState()` for other LED/resource state.
- `tests/harness/EngineHarness.*` wraps a lightweight `tracktion::engine::Engine`, our `AudioEngine`, and helper methods for generating temporary WAV samples so Tracktion-facing code can run without disk fixtures or audio hardware.

## Commit & Pull Request Guidelines
History shows short, lowercase summaries. Keep messages concise and imperative, and group mechanical updates separately from feature commits.

# Task Specific Instructions

## UI Architecture: Widget System

The UI uses a **Widget/WindowManager** architecture (not ScreenManager/ScreenView, which was removed).

### Key Classes
- `Widget` (`src/ui/widget/Widget.h`) — Base class for all views
- `WindowManager` (`src/ui/widget/WindowManager.h`) — Dual-panel layout engine
- `HardwareState` (`src/ui/hw/HardwareState.h`) — LED/resource ownership system
- `ControlTypes.h` (`src/ui/shared/`) — Option and Knob shared types
- `WidgetTypes.h` (`src/ui/widget/`) — WidgetDescriptor, DisplaySide, DisplayConstraint

### Widget Lifecycle
1. Constructor (no state access, no timers)
2. Dependency injection: `setHardware()`, `setAudioEngine()`, `setControllerHost()`
3. Resource claiming: `requiredResources(page)` -> `HardwareState::claim()`
4. `onActivated(panelOffset)` — Start timers, register handlers
5. Input handling via `handleKnob()`, `handleOption()`, `handlePad()`, `handleButton()`
6. `onDeactivated()` — Stop timers, unregister handlers
7. Resource release: `HardwareState::releaseAll(widgetId)`
8. Destructor

### Adding a New Widget
1. Create class inheriting `Widget` in `src/ui/widget/`
2. Implement `describe()` returning a `WidgetDescriptor` with unique id
3. Implement `requiredResources(page)` to declare hardware resources needed
4. Implement `getOptions(page)` and `getKnobs(page)` for bar controls
5. Implement `paintPage(g, page, bounds)` for rendering
6. Override input handlers as needed: `handlePad()`, `handleButton()`, `handleKnob()`, `handleOption()`
7. Use `onActivated(panelOffset)` / `onDeactivated()` for lifecycle
8. Open via `windowManager.open(std::make_unique<MyWidget>(), DisplaySide::Left)`

### Dual Panel Layout
- Two 480x272 physical display panels (Left and Right), total 960x272
- `panelOffset` = 0 for left, 1 for right
- Knobs k1-k4 route to left panel, k5-k8 to right panel
- Options d1-d4 route to left panel, d5-d8 to right panel

### Dialogs
- Use `WindowManager::showDialog()` / `dismissDialog()` for modal overlays
- Set `forceOnTop = true` in WidgetDescriptor
- Dialog stack supports nesting with margin

### System Widgets
- TransportWidget and GroupWidget run invisibly (no visual panel)
- They manage hardware LEDs and respond to button events
- Added via `WindowManager::addSystemWidget()`

## Handling Input / Output and State Visualization
Input should always be abstracted. Any component that wants to react to an input event should expose methods for the desired interaction. Widget base class provides `handlePad()`, `handleButton()`, `handleKnob()`, `handleOption()` virtual methods.

## Encoder / Knob Based Inputs (k1-k8)

The MK3 controller provides 8 physical rotary encoders (k1-k8). Each panel displays up to 4 knobs (k1-k4 for left, k5-k8 for right). WindowManager routes knob events to the focused panel's widget.

### Knob Configuration
Knobs are declared via `getKnobs(page)` returning `std::vector<Knob>`. Each Knob has:
- `NumericModel` or `ListModel` for value management
- `continuousMode` — true for real-time updates, false for commit-on-release
- `onAdjust` callback for rotation events
- `onInputResolved` callback for non-continuous mode (fires on knob release)
- `isInputActive` — managed by WindowManager via knobTouch events

### Critical Rules
1. **Declare knobs in `getKnobs()`** — WindowManager calls this; don't configure knobs elsewhere
2. **Don't implement custom knobTouch handlers** — WindowManager manages touch state
3. **Use `isInputActive` for state-dependent logic** — not custom touch tracking

# MK3 Specifics

The MK3 is the prototype controller for MusicPI.

## UI Design
All screens designed for Dual MusicPI Hardware Displays: 480x272 each, 960x272 total.

### Screen Layout
A typical screen consists of an optional OptionsBar, a main content area, and an optional KnobBar per panel. Each bar has 4 slots per display (d1-d4/d5-d8 for options, k1-k4/k5-k8 for knobs).

### Options Bar
4 slots per panel. Each option can be available, active, or disabled. Active state inverts font/background color (not title/label). On MK3, d1-d8 LEDs have brightness controls.

### Knob Bar
Similar to options bar with an added label (light grey, bold, h4/h5 sized). Values displayed centered in each cell. KnobTouch events highlight the cell (inverted colors).
