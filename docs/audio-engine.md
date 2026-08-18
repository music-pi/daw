# Audio engine

The engine layer wraps a Tracktion Engine `Engine` and active `Edit`. Tracktion
owns rendering, tracks, clips, plugins, transport, and device interaction;
MusicPI adds pad/pattern/song semantics and project state.

## AudioEngine

`src/engine/AudioEngine.h` is the application facade. It owns:

- the active Tracktion `Edit` and audio device configuration;
- transport, loop, record-arm, metronome, swing, and play mode;
- `.mpi`/edit load and save operations;
- the Edit's shared `juce::UndoManager`;
- application `ValueTree` children for transport, pads, groups, mixer, UI,
  and settings;
- `SamplerInstrument`, `GroupManager`, `KeyboardInstrumentBank`, global
  arpeggiator, plugin configuration, and `T9Dictionary`.

`PlayMode::Pattern` loops the active pattern. `PlayMode::Track` stages the
multi-lane song timeline. Call `updateLoopRangeForPlayMode()` after changes
that alter the relevant range.

## SamplerInstrument

`src/engine/SamplerInstrument.h` maps the MK3's 16 logical pads to Tracktion
tracks:

```text
FolderTrack "Sampler"
├── AudioTrack "Pad 1"
│   ├── MidiClip (active pattern)
│   ├── SamplerPlugin or external instrument
│   ├── effects
│   ├── VolumeAndPanPlugin
│   └── LevelMeterPlugin
├── AudioTrack "Pad 2"
└── ... Pad 16
```

Each pad's non-active patterns live in a `PadPatternBank` backed by clip slots.
Changing the active pattern archives/restores clip content. Track play mode
materialises each song block as per-pad MIDI clips on the timeline and removes
them when returning to pattern mode.

The sampler coordinates sample/instrument loading, gain, mute/solo, meters,
choke groups, pad naming/selection, sample ranges, normalisation gain, pattern
steps and velocities, pattern length, swing, song lanes, and snapshots. It
does not implement a parallel audio renderer.

Manual and sequenced playback both pass through the same Tracktion plugin
chain. Meter reads use Tracktion's `LevelMeasurer::Client`.

## Other engine services

- `GroupManager` stores and recalls eight pad snapshots and group colours.
- `KeyboardInstrumentBank` manages instrument slots and keyboard patterns.
- `SamplePreviewPlayer` uses Tracktion's file-preview Edit for browser/editor
  audition without changing the main sampler.
- `PluginCatalog` and `PluginConfigRegistry` discover VST3/LV2 plugins and
  describe pinned parameters/preset sources.
- `Arpeggiator` provides global keyboard-mode arpeggiation.

## PipeWire input recording

`Shift + Sampling` opens the dual-panel `AudioRecorderWidget`. Input capture is
kept outside the normal Tracktion output graph so opening the application does
not add dormant input channels or input-callback work:

- `pw-dump` enumerates current PipeWire `Audio/Source` nodes;
- `pw-record` exists only while REC is active and streams raw float samples to
  `PipeWireAudioRecorder`'s background WAV writer;
- `pw-loopback` exists only while D5 Monitor is active and routes the selected
  source to PipeWire's current default sink;
- accepted 48 kHz, 24-bit WAV takes live under
  `~/maschinepi/samples/recordings/`, are loaded into the selected pad through
  `LoadSampleCommand`, and then enter the regular Sampling editor.

The suite requires the standard PipeWire command-line tools (`pw-dump`,
`pw-record`, and `pw-loopback`). Monitoring defaults off, stops when the suite
closes, and is suspended during take preview so the recorded audio is heard in
isolation.

## Undoable commands

User-visible reversible mutations use `juce::UndoableAction` implementations
under `src/engine/commands/`, executed through the AudioEngine undo manager:

```cpp
auto& undo = audioEngine.getUndoManager();
undo.beginNewTransaction("Load Sample");
undo.perform(new LoadSampleCommand(audioEngine, padIndex, file));
```

Commands cover sequencing, tempo/swing, sample loading and processing,
gain/choke settings, plugin insertion/removal, instruments, and slice points.
Use the directory contents as the authoritative command list.

Raw sampler mutation variants exist for command `perform()`/`undo()` paths so
they do not recursively enter Tracktion's undo manager.

## Signal flow

```text
per-pad MidiClip or live trigger
  -> SamplerPlugin / external instrument
  -> inserted effects
  -> VolumeAndPanPlugin
  -> LevelMeterPlugin
  -> FolderTrack mix
  -> Tracktion output graph
  -> ALSA/PipeWire/JACK device
```

The discovery and file-browser preview paths use `SamplePreviewPlayer`; once a
file is loaded into a pad it enters the normal signal flow above.

## Persistence

`.mpi` is the application project workflow. The project combines the
Tracktion edit with MusicPI state needed to restore pads, patterns, song
lanes, groups, mixer/UI state, plugin configuration, text data, and discovery
sessions. Sample paths are resolved relative to project/user sample locations
where possible.

Do not serialize a second copy of data Tracktion already owns unless a stable
MusicPI snapshot is explicitly required for group recall or migration.

## Real-time rules

- No filesystem/network access or UI work on the audio thread.
- Avoid locks and allocation on hot render/meter paths.
- Perform Tracktion/ValueTree mutations on the message thread.
- Use worker pools for discovery, indexing, decoding, and analysis.
- Use snapshots/atomics for UI reads that cross thread boundaries.

Engine-facing tests should use `tests/harness/EngineHarness` so they run
without physical audio hardware.
