# ValueTree Migration Inventory

> **Historical snapshot:** wildcard paths and migration status below reflect
> an earlier repository state. Current ownership is summarised in
> [architecture.md](architecture.md) and `AudioEngine.h`.

Collected on 2025-11-19 to support the migration plan in `/valu-378031.plan.md`. This document lists the current mutable state holders across `src/` grouped by subsystem, with the data they manage, how they mutate it today, and why each piece should migrate into a shared `juce::ValueTree` + `juce::UndoManager`.

## Engine Layer

- `src/engine/AudioEngine.*`
  - Owns global playback state: `te::Edit`, `paused`, `currentProjectFile`, transport flags (`TransportSnapshot`), and singletons (`SamplerInstrument`, `SamplerInstrument`, `GroupManager`, `UndoManager`).
  - Serialises pad banks into ad-hoc XML nodes (`kProjectPadNode`) via snapshot structs.
  - **Migration target**: Introduce root `juce::ValueTree maschinepiState` under `AudioEngine`, with child trees for transport (`transport`), pads (`pads`), groups (`groups`), mixer (`padMixer`), UI/session (`uiState`).
  - **Entry points**: project load/save (`loadProjectFromFile`, `saveProjectToFile`), transport control methods (`play`, `stop`, `toggleLoop`, etc.) should wrap their ValueTree mutations inside `UndoManager::beginNewTransaction`.

- `src/engine/SamplerInstrument.*`
  - Stores pad metadata in `std::vector<Pad> pads`, pattern data in `std::vector<Pattern> patterns`, selection state (`selectedPadId`), and listeners (`juce::ListenerList`).
  - Exposes numerous mutators: `addPad`, `loadSampleForPad`, `setPadChokeGroup`, `setSequencerStep`, `toggleSequencerStep`, etc. Snapshots are rebuilt each time via `getPadsSnapshot()`.
  - **Migration target**: Replace `pads`/`patterns` arrays with hierarchical ValueTree nodes:
    - `pads` child containing `pad` nodes with properties mirroring `PadSnapshot`.
    - Each `pad` contains `patterns` node, then `pattern` nodes containing `lanes` children.
  - Selection state should be a ValueTree property (e.g. `uiState.selectedPadId`) so UI and hardware can observe changes.
  - Diff listeners should mirror tree changes into Tracktion (`ensureSamplerPlugin`, `applyPadGain`, etc.) instead of re-reading snapshots.

- `src/engine/SamplerInstrument.*`
  - Maintains `std::vector<std::shared_ptr<PadChannel>> channels`, `indexByPadId`, and per-pad gain/envelope fields stored atomically in `PadChannel`.
  - Methods like `setPadGainDb`, `setPadSampleWindow`, `triggerPad` mutate per-channel state directly.
  - **Migration target**: Store `padMixer` node per pad containing gain/level + sample window metadata. Mixer listens for ValueTree property changes and applies them atomically to channels. Level meters should write back into the tree so UI sees live telemetry (optionally throttled).

- `src/engine/GroupManager.*`
  - Uses `std::array<Group, 8> groups`, each `Group` contains `std::optional<std::vector<SamplerInstrument::PadSnapshot>>` (full copies), `color`, `isActive`. Active group index maintained separately.
  - Save/recall simply copies snapshots in/out of `SamplerInstrument`.
  - **Migration target**: Represent groups as `groups` tree with `group` nodes storing properties:
    - `color`, `isActive`, `lastSavedAt`, child `pads` tree referencing pad IDs (or full pad subtrees).
    - Undo/redo becomes trivial by editing the tree.

- `src/engine/UndoManager.*` and `src/engine/commands/*`
  - Custom command stack (`std::deque`, `currentIndex`, `maxHistorySize`) duplicates JUCE undo functionality.
  - Concrete command classes (e.g. `AddCutPointCommand`) mutate model objects manually.
  - **Migration target**: Replace with shared `juce::UndoManager`. Each command path becomes a transaction that adjusts ValueTree properties, letting JUCE snapshots handle history. Legacy command objects can be retired after ValueTree adoption.

## Control / Input Layer

- `src/control/ControllerHost.*`
  - Tracks controller bindings in `std::unordered_map` (`buttonBindings`, `buttonActiveStates`), pad listeners vector, `shiftPressed/macroPressed`, LED states indirectly via Mk3 driver.
  - Maintains `padSelectionCallback` plus asynchronous dispatchers.
  - **Migration usage**: Instead of bespoke callbacks toggling LED brightness, expose/consume ValueTree nodes (`controller.buttons.<id>.active`, `controller.knobs.<id>.touch`) so hardware updates respond to tree diffs. Button presses write into intent nodes (e.g. `input.lastButtonEvent`).

- `src/input/InputManager.*`
  - Owns registered handlers vector and active context map; acts as event router.
  - While registry itself may remain imperative, resulting state changes (pad selection, transport actions) should mutate ValueTree branches instead of private members inside views/controllers.

- `EncoderHandler`, `ControllerInputHandler`
  - Maintain their own binding IDs and selection caches.

## UI Layer

- `src/ui/screen/ScreenManager.*`
  - Tracks pane stacks (`ScreenRouter left/right`), button ID arrays, knob mapping arrays, `std::array<bool, 8> knobTouchState`, and per-pane option LED brightness. `setKnobInputActive` flows by comparing physical arrays.
  - **Migration target**: Mirror UI navigation inside ValueTree (`uiState.screens.left.stack`, `uiState.knobs.touch[0-7]`). Controller events set `knobs.touch[N]` booleans; `ScreenView` listens for the subset it owns.

- `src/ui/views/*`
  - Each view caches its current model: e.g.
    - `PadDetailsView` stores `currentPad`, `padTitle`, `sampleStatus`, `hasSelection`.
    - `PadOverviewView` likely tracks grid selection, focus, multi-select.
    - `AudioEditor` owns waveform selection, slicing markers, knob states.
  - Views poll `AudioEngine` for snapshots (e.g. `refreshSelection()` pulling from `SamplerInstrument`).
  - **Migration target**: Views subscribe to relevant ValueTree subtrees (pads, transport, mixer). They render derived data directly from the tree and push edits by writing new property values (e.g. knob adjustments set `pads/<id>/chokeGroup`). Undo becomes automatic.

- `src/ui/components/*`
  - Components like `OptionsBarComponent`, `KnobBarComponent`, `ListComponent` maintain local arrays representing slot states.
  - They should instead read from `ScreenView`'s ValueTree-backed state, or at least propagate ValueTree listeners down so they rebuild only when the underlying tree changes.

- `src/ui/UiHost.*` & `ScreenRouter.*`
  - Manage active screen, window focus, and controller coupling.
  - Need ValueTree nodes for global session state (active pane, modal dialogs, sample browser visibility) so host, controller, and tests observe consistent data.

## Devices Layer

- `src/devices/Mk3Device.*`
  - Wraps lower-level C driver; tracks connection state, LED buffers, display frame caches.
  - Should observe hardware-related ValueTree paths (e.g. `controller.leds.D1.brightness`) and push deltas to USB driver, rather than being invoked ad-hoc from `ControllerHost`.

## Tests

- `tests/*.cpp`
  - Current tests operate on direct structs (e.g. `ScreenManagerTests` assert knob arrays). After migration they must create `juce::ValueTree` fixtures and assert property changes + undo behaviour.

## Summary

All mutable state that influences UI, controller LEDs, transport, pads, and groups currently lives in bespoke structs/vectors spread across Engine, UI, and Control modules. Converging these onto a single ValueTree hierarchy rooted in `AudioEngine` enables:

1. **Undo/Redo**: `juce::UndoManager` can snapshot property diffs instead of manual `Command` subclasses.
2. **Single Source of Truth**: UI views, controller host, and engine code read/write the same tree instead of exchanging snapshots or direct pointers.
3. **Serialization**: Saving projects becomes `ValueTree::createXml`, while loading simply restores the tree before Tracktion sync.

Next steps per plan: add the shared root tree/UndoManager, migrate SamplerInstrument + group/mixer state, bind UI & controller logic to tree listeners, then expose undo/redo affordances with regression tests.
