# Tracktion Engine Native Architecture

> **Historical proposal:** many names and migration steps below describe the
> pre-refactor codebase. See [audio-engine.md](audio-engine.md) for the current
> implementation.

## Overview

This document describes the TE-native architecture for MusicPI. This design is fully implemented — see [audio-engine.md](audio-engine.md) for current API documentation.

## Design Principles

1. **Use TE's track system** - Every mixable element is a Track
2. **Use TE's plugins** - SamplerPlugin, VolumeAndPanPlugin, LevelMeterPlugin
3. **Use TE's routing** - FolderTracks for grouping/busing
4. **No custom audio rendering** - All audio rendered by TE plugins

## Track Hierarchy

```
Edit
├── FolderTrack "Sampler"          ← Global mixer shows this as ONE channel
│   ├── AudioTrack "Pad 1"         ← Sampler mixer shows individual pads
│   │   ├── SamplerPlugin          (1 sound per pad)
│   │   ├── VolumeAndPanPlugin     (gain, pan)
│   │   └── LevelMeterPlugin       (RMS/peak metering)
│   ├── AudioTrack "Pad 2"
│   │   └── [same plugins]
│   ├── ...
│   └── AudioTrack "Pad 16"
│       └── [same plugins]
│
├── MidiTrack "Sampler Seq"        ← Pattern sequencer
│   └── StepClip
│       └── Patterns with steps that route to pad tracks
│
├── FolderTrack "Drums"            ← Another instrument (future)
│   └── [similar structure]
│
└── MasterTrack                    ← Master output
    ├── VolumeAndPanPlugin
    └── LevelMeterPlugin
```

## Mixer Views

### Global Mixer
Shows FolderTracks as channels:
- "Sampler" (sum of all 16 pads)
- "Drums" (future)
- "Master"

User can navigate into a FolderTrack to see its children.

### Sampler Mixer (drill-down)
Shows the 16 AudioTracks inside "Sampler" folder:
- Pad 1, Pad 2, ... Pad 16
- Each with gain fader, pan, mute/solo, meters

### Navigation
- **Shift+Pad** in global view → drill into that instrument's mixer
- **Back button** → return to global mixer

## Audio Signal Flow

```
┌─────────────────────────────────────────────────────────────┐
│ Pad Track "Pad 1"                                           │
│  ┌──────────────┐   ┌─────────────────┐   ┌──────────────┐ │
│  │ SamplerPlugin│ → │VolumeAndPanPlugin│ → │LevelMeterPlugin│ │
│  │ (audio out)  │   │ (gain/pan)       │   │ (metering)    │ │
│  └──────────────┘   └─────────────────┘   └──────────────┘ │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│ FolderTrack "Sampler" (implicit sum of child tracks)        │
│  ┌─────────────────┐   ┌──────────────┐                     │
│  │VolumeAndPanPlugin│ → │LevelMeterPlugin│                    │
│  │ (bus gain/pan)  │   │ (bus metering)│                    │
│  └─────────────────┘   └──────────────┘                     │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│ MasterTrack                                                 │
│  ┌─────────────────┐   ┌──────────────┐                     │
│  │VolumeAndPanPlugin│ → │LevelMeterPlugin│                    │
│  └─────────────────┘   └──────────────┘                     │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
                      Audio Device Output
```

## MIDI Routing for Triggering

### Option A: StepClip with MIDI routing
```
StepClip → MIDI Output → Pad Track MIDI Input → SamplerPlugin
```
Each pad track receives MIDI on a specific channel/note.

### Option B: Direct triggering via TE API
```cpp
// Hardware pad press
void onPadPressed(int padIndex, float velocity) {
    auto* padTrack = getPadTrack(padIndex);
    auto* sampler = padTrack->pluginsOfType<te::SamplerPlugin>().getFirst();
    sampler->playNote(60, velocity);  // Note 60, or configurable
}
```

We'll use Option B for immediate hardware response, Option A for sequencer playback.

## Implementation: SamplerPadBank Refactor

### Current API (keep compatible)
```cpp
int addPad(const juce::String& name, const juce::File& sampleFile);
bool loadSampleForPad(int padId, const juce::File& sampleFile);
void setPadGainDb(int padId, float gainDb);
float getPadGainDb(int padId) const;
bool triggerPadById(int padId, float velocity);
```

### Internal changes
```cpp
// OLD: Single samplerTrack with PadMixerPlugin
tracktion::engine::AudioTrack::Ptr samplerTrack;
tracktion::engine::SamplerPlugin* samplerPlugin;  // 16 sounds in one plugin

// NEW: FolderTrack with per-pad AudioTracks
tracktion::engine::FolderTrack::Ptr samplerFolder;
std::array<PadTrackInfo, 16> padTracks;

struct PadTrackInfo {
    te::AudioTrack::Ptr track;
    te::SamplerPlugin* sampler;       // 1 sound per plugin
    te::VolumeAndPanPlugin* volume;
    te::LevelMeterPlugin* meter;
};
```

## Implementation: MixerView Refactor

### Current
```cpp
enum class MixerMode { Sampler, Arranger };
// Completely different code paths for each mode
```

### New
```cpp
// Unified track-based view
te::Track* currentMixerRoot;  // nullptr = show top-level tracks

void setMixerRoot(te::Track* root) {
    if (auto* folder = dynamic_cast<te::FolderTrack*>(root)) {
        // Show folder's children
        showTracks(folder->getAllSubTracks(false));
    } else {
        // Show top-level tracks
        showTracks(te::getAllTracks(*edit));
    }
}
```

## Files to Remove

- `src/engine/PadAudioMixer.h` - custom audio mixer
- `src/engine/PadAudioMixer.cpp`
- `src/engine/PadMixerPlugin.h` - bridge plugin
- `src/engine/PadMixerPlugin.cpp`

## Files to Modify

- `src/engine/SamplerPadBank.h/cpp` - per-pad tracks, TE-native mixing
- `src/engine/AudioEngine.h/cpp` - remove PadAudioMixer usage
- `src/ui/views/MixerView.h/cpp` - unified track-based view
- `CMakeLists.txt` - remove deleted files

## Migration Steps

1. Create FolderTrack + per-pad AudioTracks structure
2. Add SamplerPlugin + VolumeAndPanPlugin + LevelMeterPlugin per pad
3. Update gain/trigger methods to use TE plugins
4. Update metering to use LevelMeterPlugin
5. Update MixerView to navigate track hierarchy
6. Remove PadAudioMixer and PadMixerPlugin
7. Update tests

## Benefits

1. **Single source of truth** - TE manages all audio routing
2. **Native metering** - LevelMeterPlugin provides accurate levels
3. **Native mixing** - VolumeAndPanPlugin handles gain/pan
4. **Hierarchical mixer** - FolderTracks enable drill-down views
5. **Future-proof** - Easy to add effects, sends, automation
6. **Less code** - ~800 lines removed
