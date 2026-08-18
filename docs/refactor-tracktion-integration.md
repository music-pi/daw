# Tracktion Engine Deep Integration Refactor Plan

> **Historical plan:** this document predates the current TE-native sampler,
> per-pad MIDI clips, and Widget UI. It is retained as design history, not as
> an implementation guide. See [audio-engine.md](audio-engine.md) and
> [architecture.md](architecture.md) for the current system.

> **COMPLETED** — This refactor plan was fully executed. PadAudioMixer and PadMixerPlugin
> have been removed. SamplerInstrument now uses TE-native tracks with SamplerPlugin,
> VolumeAndPanPlugin, and LevelMeterPlugin per pad. See [te-native-architecture.md](te-native-architecture.md)
> for the current design and [audio-engine.md](audio-engine.md) for current API documentation.

## Overview

This document outlines a phased approach to eliminate custom audio processing code by leveraging Tracktion Engine (TE) native functionality. The goal is to simplify the codebase using a **facade pattern** - creating clean interfaces that delegate to TE internals.

**Current state:** Hybrid architecture with custom `PadAudioMixer` + TE
**Target state:** Unified TE-native audio pipeline with thin facades
**Estimated reduction:** ~830 lines (30% of audio engine code)

---

## Architecture Vision

### Current (Problematic)
```
User Input
    ↓
SamplerPadBank
    ↓
┌─────────────────────────────────────────┐
│ SPLIT PATH (source of complexity)       │
├─────────────────────────────────────────┤
│ Direct Pad:     │ Pattern Playback:     │
│ PadAudioMixer   │ StepClip → Sampler    │
│ (custom render) │ (TE native)           │
└────────┬────────┴───────────┬───────────┘
         ↓                    ↓
   PadMixerPlugin ←────→ TE Audio Graph
         ↓
    Audio Output
```

### Target (Unified)
```
User Input
    ↓
SamplerPadBank (facade layer)
    ↓
┌─────────────────────────────────────────┐
│ UNIFIED PATH via Tracktion Engine       │
├─────────────────────────────────────────┤
│ te::SamplerPlugin (sample playback)     │
│       ↓                                 │
│ te::VolumeAndPanPlugin (gain/pan)       │
│       ↓                                 │
│ te::LevelMeterPlugin (metering)         │
│       ↓                                 │
│ te::TrackOutput (routing)               │
└─────────────────────────────────────────┘
         ↓
    TE Audio Graph → Audio Output
```

---

## Phase 1: Facade Interfaces (Foundation)

### Goal
Create abstraction interfaces that can delegate to either custom or TE implementation, enabling gradual migration.

### Files to Create

#### `src/engine/facades/IPadMixer.h`
```cpp
#pragma once
#include <juce_core/juce_core.h>

// Facade interface for pad mixing operations
class IPadMixer
{
public:
    virtual ~IPadMixer() = default;

    // Sample management
    virtual bool loadSample(int padId, const juce::File& file) = 0;
    virtual void clearSample(int padId) = 0;
    virtual bool hasSample(int padId) const = 0;

    // Playback
    virtual void triggerPad(int padId, float velocity) = 0;
    virtual void stopPad(int padId) = 0;
    virtual void stopAll() = 0;
    virtual bool isPadPlaying(int padId) const = 0;

    // Gain control
    virtual void setPadGainDb(int padId, float gainDb) = 0;
    virtual float getPadGainDb(int padId) const = 0;

    // Metering
    virtual float getLevelRms(int padId) const = 0;
    virtual float getLevelPeak(int padId) const = 0;

    // Sample info
    struct SampleInfo {
        juce::File file;
        double sampleRate;
        int totalSamples;
        int windowStart;
        int windowEnd;
        double fadeInSeconds;
        double fadeOutSeconds;
    };
    virtual std::optional<SampleInfo> getSampleInfo(int padId) const = 0;

    // Sample editing
    virtual bool setSampleWindow(int padId, int startSample, int endSample) = 0;
    virtual bool setSampleEnvelope(int padId, int fadeInSamples, int fadeOutSamples) = 0;
};
```

#### `src/engine/facades/TracktionPadMixer.h`
```cpp
#pragma once
#include "IPadMixer.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

// Tracktion Engine native implementation of IPadMixer
class TracktionPadMixer : public IPadMixer
{
public:
    TracktionPadMixer(te::Edit& edit);

    // IPadMixer implementation delegating to TE
    bool loadSample(int padId, const juce::File& file) override;
    void triggerPad(int padId, float velocity) override;
    float getLevelRms(int padId) const override;
    float getLevelPeak(int padId) const override;
    // ... etc

private:
    te::Edit& edit;
    te::AudioTrack* samplerTrack = nullptr;
    te::SamplerPlugin* samplerPlugin = nullptr;
    te::VolumeAndPanPlugin* volumePlugin = nullptr;
    te::LevelMeterPlugin* levelMeterPlugin = nullptr;

    std::unordered_map<int, int> padIdToSoundIndex;
};
```

### Files to Modify

| File | Changes |
|------|---------|
| `SamplerPadBank.h` | Add `std::unique_ptr<IPadMixer> mixer` member |
| `SamplerPadBank.cpp` | Replace direct `PadAudioMixer` calls with `mixer->` calls |
| `AudioEngine.h` | Expose facade factory method |

### Verification
- All existing tests pass (no behavior change)
- New unit tests for facade interfaces
- Build with no warnings

---

## Phase 2: Metering Consolidation

### Goal
Replace custom RMS/Peak calculations with `te::LevelMeterPlugin`.

### Current Code (Remove)
```cpp
// PadAudioMixer.cpp lines 166-180, 619-640
float PadAudioMixer::getPadLevelRms(int padId) const {
    if (auto channel = getChannelForPad(padId))
        return channel->levelRms.load(std::memory_order_relaxed);
    return 0.0f;
}

// Manual calculation in renderAudio():
float sumSquares = 0.0f;
for (int s = 0; s < numSamples; ++s)
    sumSquares += sample * sample;
float rms = std::sqrt(sumSquares / numSamples);
```

### Replacement (TE Native)
```cpp
// TracktionPadMixer.cpp
float TracktionPadMixer::getLevelRms(int padId) const {
    if (auto* meter = samplerTrack->getLevelMeterPlugin())
        return meter->getLevel(0); // Left channel, or average
    return 0.0f;
}

float TracktionPadMixer::getLevelPeak(int padId) const {
    if (auto* meter = samplerTrack->getLevelMeterPlugin())
        return meter->getPeakLevel(0);
    return 0.0f;
}
```

### Files to Modify

| File | Changes |
|------|---------|
| `TracktionPadMixer.cpp` | Implement metering via `LevelMeterPlugin` |
| `PadAudioMixer.cpp` | Remove metering calculations (lines 166-180, 519-641) |

### Lines Removed: ~60

### Verification
- Meter UI displays correctly
- Metering responds to actual playback
- No audio glitches

---

## Phase 3: Gain Control Consolidation

### Goal
Replace custom gain application with `te::VolumeAndPanPlugin`.

### Current Code (Remove)
```cpp
// PadAudioMixer.cpp lines 147-164, 570-572
void PadAudioMixer::setPadGainDb(int padId, float gainDb) {
    if (auto channel = getChannelForPad(padId))
        channel->gainLinear.store(juce::Decibels::decibelsToGain(gainDb));
}

// In renderAudio():
const float gain = channel->gainLinear.load() * channel->velocity.load();
sample *= gain; // Manual gain application
```

### Replacement (TE Native)
```cpp
// TracktionPadMixer.cpp
void TracktionPadMixer::setPadGainDb(int padId, float gainDb) {
    if (auto* vol = samplerTrack->getVolumePlugin())
        vol->setVolumeDb(gainDb);
}
```

### Per-Pad vs Track Gain
**Challenge:** TE's `VolumeAndPanPlugin` is per-track, not per-sample.

**Solutions:**
1. **SamplerPlugin sound gains:** Use `samplerPlugin->setSoundGains(soundIndex, gain, pan)`
2. **Multiple tracks:** One track per pad (memory overhead)
3. **Hybrid:** Use `SamplerPlugin` for per-pad gain, `VolumePlugin` for master

**Recommended:** Option 1 - `SamplerPlugin::setSoundGains()` already supports per-sound gain.

### Files to Modify

| File | Changes |
|------|---------|
| `TracktionPadMixer.cpp` | Implement gain via `SamplerPlugin::setSoundGains()` |
| `SamplerPadBank.cpp` | Remove dual gain application (line 1251) |
| `PadAudioMixer.cpp` | Remove gain logic (lines 147-164, 570-572) |

### Lines Removed: ~100

### Verification
- Pad volume changes reflect in audio output
- Sequencer and direct triggers have same gain behavior
- No clipping or unexpected level changes

---

## Phase 4: Sample Loading Unification

### Goal
Eliminate dual sample loading (currently loading into both `PadAudioMixer` and `SamplerPlugin`).

### Current Code (Wasteful)
```cpp
// SamplerPadBank.cpp loadSampleForPad()
samplerPlugin->addSound(sampleFile, ...);  // Load #1
audioEngine.getPadAudioMixer().setSampleForPad(padIt->id, sampleFile);  // Load #2 (duplicate!)
```

### Replacement
```cpp
// TracktionPadMixer.cpp
bool TracktionPadMixer::loadSample(int padId, const juce::File& file) {
    // Single load via SamplerPlugin
    auto error = samplerPlugin->addSound(
        file.getFullPathName(),
        file.getFileNameWithoutExtension(),
        0.0, 0.0, 0.0f
    );
    if (error.isNotEmpty())
        return false;

    padIdToSoundIndex[padId] = samplerPlugin->getNumSounds() - 1;
    return true;
}
```

### Sample Info Access
Use `te::SamplerPlugin::SamplerSound` for metadata instead of custom `PadChannel`:

```cpp
std::optional<SampleInfo> TracktionPadMixer::getSampleInfo(int padId) const {
    auto it = padIdToSoundIndex.find(padId);
    if (it == padIdToSoundIndex.end())
        return std::nullopt;

    if (auto* sound = samplerPlugin->getSound(it->second)) {
        SampleInfo info;
        info.file = sound->getSourceFile();
        info.sampleRate = sound->getSampleRate();
        info.totalSamples = sound->getLengthInSamples();
        // Window/envelope from sound properties
        return info;
    }
    return std::nullopt;
}
```

### Files to Modify

| File | Changes |
|------|---------|
| `TracktionPadMixer.cpp` | Implement single-path loading |
| `SamplerPadBank.cpp` | Remove `getPadAudioMixer().setSampleForPad()` calls |
| `PadAudioMixer.cpp` | Remove `setSampleForPad()`, `resampleIfNeeded()` (lines 77-110) |

### Lines Removed: ~200

### Verification
- Sample loading works for all pads
- Sample preview still works
- No memory leaks (single allocation per sample)

---

## Phase 5: Direct Pad Triggering via TE

### Goal
Route direct pad triggers through `SamplerPlugin` instead of custom `PadAudioMixer::renderAudio()`.

### Current Code
```cpp
// SamplerPadBank.cpp triggerPad()
audioEngine.getPadAudioMixer().triggerPad(pad.id, velocity);  // Custom render
```

### Replacement
```cpp
// TracktionPadMixer.cpp
void TracktionPadMixer::triggerPad(int padId, float velocity) {
    auto it = padIdToSoundIndex.find(padId);
    if (it == padIdToSoundIndex.end())
        return;

    const int midiNote = 36 + it->second; // Or use stored note number
    const int midiVelocity = static_cast<int>(velocity * 127.0f);

    // Use SamplerPlugin's playNotes API
    juce::BigInteger keysDown;
    keysDown.setBit(midiNote);
    samplerPlugin->playNotes(keysDown);
}
```

### Choke Groups
Implement via MIDI note-off to existing sounds in same group:
```cpp
void TracktionPadMixer::applyChokeGroup(int padId) {
    const int group = padChokeGroups[padId];
    if (group == 0) return; // No choke group

    for (auto& [otherId, soundIdx] : padIdToSoundIndex) {
        if (otherId != padId && padChokeGroups[otherId] == group) {
            // Stop other pad via note-off
            const int note = 36 + soundIdx;
            samplerPlugin->allNotesOff(); // Or specific note-off
        }
    }
}
```

### Files to Modify

| File | Changes |
|------|---------|
| `TracktionPadMixer.cpp` | Implement `triggerPad()` via `SamplerPlugin` |
| `SamplerPadBank.cpp` | Remove custom trigger path |
| `PadAudioMixer.cpp` | Remove `triggerPad()`, `renderAudio()` (lines 122-145, 284-314, 486-511) |

### Lines Removed: ~250

### Verification
- Direct pad hits produce audio
- Velocity affects volume
- Choke groups work correctly
- Sequencer playback unchanged

---

## Phase 6: Remove PadMixerPlugin

### Goal
Eliminate the custom Tracktion plugin wrapper now that all audio goes through native TE.

### Current Code (Remove Entirely)
```cpp
// PadMixerPlugin.h/cpp - 70 lines
class PadMixerPlugin : public te::Plugin {
    void applyToBuffer(const PluginRenderContext& ctx) override {
        padAudioMixer->renderAudio(ctx.destBuffer); // Bridge layer
    }
};
```

### Replacement
No replacement needed - `SamplerPlugin` + `VolumePlugin` handle everything.

### Files to Delete
- `src/engine/PadMixerPlugin.h`
- `src/engine/PadMixerPlugin.cpp`

### Files to Modify

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Remove PadMixerPlugin from sources |
| `AudioEngine.cpp` | Remove plugin registration, `ensurePadMixerPlugin()` |
| `SamplerPadBank.cpp` | Remove `ensurePadMixerPlugin()` calls |

### Lines Removed: ~70

### Verification
- Build succeeds without PadMixerPlugin
- Audio playback works
- All tests pass

---

## Phase 7: PadAudioMixer Removal

### Goal
Complete removal of custom audio mixing code.

### Files to Delete
- `src/engine/PadAudioMixer.h`
- `src/engine/PadAudioMixer.cpp`

### Files to Modify

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Remove PadAudioMixer from sources |
| `AudioEngine.h/cpp` | Remove `padAudioMixer` member |
| `SamplerPadBank.cpp` | Use `IPadMixer` facade exclusively |

### What to Keep (Move to TracktionPadMixer if needed)
- Sample preview player (may use separate temporary AudioClip)
- File resolution/caching logic (move to facade)

### Lines Removed: ~650

### Verification
- Full test suite passes
- Sample preview works
- All mixer functionality intact

---

## Phase 8: State Consolidation

### Goal
Unify state management to use TE's native ValueTree where possible.

### Current State Trees
```cpp
// AudioEngine.h - separate trees
juce::ValueTree appState;
juce::ValueTree transportState;
juce::ValueTree padsState;
juce::ValueTree groupsState;
juce::ValueTree mixerState;
juce::ValueTree uiState;
```

### Target
```cpp
// Use Edit's ValueTree for audio state
// Keep separate trees only for UI/app state
te::Edit& edit;  // Contains transport, tracks, clips
juce::ValueTree appState;   // App settings
juce::ValueTree uiState;    // UI preferences
// padsState, groupsState, mixerState -> integrated into Edit
```

### Benefits
- Native undo/redo via Edit
- Automatic persistence
- TE handles state consistency

---

## Summary

### Code Reduction by Phase

| Phase | Lines Removed | Cumulative |
|-------|--------------|------------|
| 1: Facades | 0 (add ~150) | +150 |
| 2: Metering | ~60 | +90 |
| 3: Gain | ~100 | -10 |
| 4: Loading | ~200 | -210 |
| 5: Triggering | ~250 | -460 |
| 6: PadMixerPlugin | ~70 | -530 |
| 7: PadAudioMixer | ~650 | -830* |
| 8: State | ~50 | -880 |

*Net reduction accounting for new facade code

### Timeline Estimate

| Phase | Effort | Dependencies |
|-------|--------|--------------|
| 1 | 2-3 days | None |
| 2 | 1 day | Phase 1 |
| 3 | 1-2 days | Phase 1 |
| 4 | 2-3 days | Phase 1 |
| 5 | 3-4 days | Phases 3, 4 |
| 6 | 0.5 day | Phase 5 |
| 7 | 1 day | Phase 6 |
| 8 | 2 days | Phase 7 |

**Total: ~2-3 weeks**

### Risk Mitigation

1. **Each phase is independently testable** - can stop at any phase
2. **Facade pattern allows rollback** - swap implementation behind interface
3. **Incremental migration** - old and new code can coexist during transition
4. **Comprehensive test coverage** before each phase

---

## Next Steps

1. Review and approve this plan
2. Create Phase 1 facade interfaces
3. Add test coverage for current behavior
4. Begin incremental migration

---

## Appendix: Key Tracktion Engine APIs

### SamplerPlugin
```cpp
// Sample management
juce::String addSound(const juce::String& file, ...);
SamplerSound* getSound(int index);
void setSoundGains(int index, float gain, float pan);
void setSoundExclusiveGroup(int index, int group);

// Playback
void playNotes(juce::BigInteger keysDown);
void allNotesOff();
```

### VolumeAndPanPlugin
```cpp
void setVolumeDb(float db);
float getVolumeDb() const;
void setPan(float pan);
```

### LevelMeterPlugin
```cpp
float getLevel(int channel) const;
float getPeakLevel(int channel) const;
void resetPeakLevel();
```

### AudioTrack
```cpp
VolumeAndPanPlugin* getVolumePlugin();
LevelMeterPlugin* getLevelMeterPlugin();
void insertPlugin(Plugin*, int position);
```
