#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <tracktion_engine/tracktion_engine.h>

#include "PadPatternBank.h"
#include "SamplerInstrument.h"

namespace te = tracktion::engine;

class AudioEngine;

/** Instrument slots under a dedicated "Instruments" folder track. Each slot
    owns one AudioTrack + instrument Plugin + MidiClip so instruments stay out
    of the sampler's pad namespace. */
class KeyboardInstrumentBank
{
public:
    struct Slot
    {
        te::AudioTrack::Ptr track;
        te::Plugin* plugin { nullptr };
        te::MidiClip::Ptr midiClip;
        juce::String displayName;
        // Per-pattern MidiClip archive — slot.midiClip is the live clip for
        // the active pattern; patternBank holds the rest via track's
        // ClipSlotList.
        PadPatternBank patternBank;
        // Song-mode materialisation — one MidiClip per SongBlock across every
        // lane, placed on slot.track at the block's bar range. Parallel to
        // SamplerInstrument::PadInfo::materializedSlotClips. Built by
        // materializeSong(), torn down by dematerializeSong().
        std::vector<te::MidiClip::Ptr> materializedClips;
    };

    explicit KeyboardInstrumentBank(AudioEngine&);
    ~KeyboardInstrumentBank();

    void attachToEdit(te::Edit&);
    void detach();

    [[nodiscard]] int  getNumSlots() const noexcept { return static_cast<int>(slots_.size()); }
    [[nodiscard]] int  getActiveSlot() const noexcept { return activeSlot_; }
    [[nodiscard]] bool isOnEmptySlot() const noexcept { return activeSlot_ >= getNumSlots(); }

    /** Clamps to [0, numSlots]. numSlots == virtual empty cursor. */
    void setActiveSlot(int index);
    void nextSlot();
    void prevSlot();

    /** Load an instrument. On the empty slot this appends a new slot; on a
        populated slot it replaces that slot's plugin. Returns false if
        track creation or plugin instantiation fails. */
    bool loadInstrument(const juce::PluginDescription& desc);

    /** Remove the active slot's track. Cursor falls back to the first
        remaining slot, or the empty cursor if none. */
    void deleteActiveSlot();

    te::AudioTrack* getTrack() const;       // active slot's track
    te::Plugin*     getPlugin() const;      // active slot's instrument source
    te::MidiClip*   getMidiClip() const;    // active slot's MidiClip
    juce::String    getActiveName() const;  // active slot's display name
    [[nodiscard]] te::FolderTrack* getFolder() const noexcept
    {
        return folder_.get();
    }

    /** Read-only access to a specific slot (for PatternWidget iteration). */
    const Slot* getSlot(int index) const;

    /** Serialisation. UiHost's save/load plumbing passes a ValueTree child
        under maschinepi_state so instrument slots persist. */
    juce::ValueTree toState() const;
    void restoreFromState(const juce::ValueTree&);

    /** Swap each slot's live MidiClip to the requested pattern index. The
        currently-live content is archived into the outgoing bank slot; the
        new index is restored into the live clip. No-op when newIndex equals
        the current active pattern. */
    void setActivePatternIndex(int newIndex);

    [[nodiscard]] int getActivePatternIndex() const noexcept { return activePatternIndex_; }

    /** Write every slot's live clip into the bank at the given pattern
        index without touching the live clip. Used by song materialisation
        so current edits survive a swap. */
    void archiveLiveClipsTo(int patternIndex);

    /** Wipe every slot's notes for the given pattern index. For the active
        pattern the live clip is cleared + archived; for inactive patterns
        only the bank slot is touched. Pad/drum content is left alone —
        drum + keyboard clears scope to their own bank. */
    bool clearPattern(int patternIndex);

    /** Per-slot parallel to SamplerInstrument's pad materialisation. Parks
        each slot's live MidiClip past the song end and inserts one MidiClip
        per SongBlock across `lanes`, copying the referenced pattern's bank
        content into each new clip. Empty bank slots produce silent blocks
        (correct — user didn't record on that pattern for that slot). */
    void materializeSong(const std::vector<SamplerInstrument::SongLane>& lanes);

    /** Reverse of materializeSong: removes every placed clip and restores
        each slot's live MidiClip to bar 0..1 so pattern-mode playback sees
        the canonical live clip. Safe no-op if nothing was materialised. */
    void dematerializeSong();

    /** Resize each slot's live MidiClip to `bars * beatsPerBar` beats. Mirrors
        SamplerInstrument::setPatternLength so keyboard slots stay in lockstep
        with the pad grid. patternIndex is accepted for API parity. */
    bool setPatternLength(int patternIndex, int bars, int stepsPerBar);

    /** Stamp the Basic 16th Swing groove template + strength onto every
        slot's live clip. Mirrors SamplerInstrument::applySwing so keyboard
        slots groove identically to pads. */
    void applySwing(float strength);

    /** Insert an empty ClipSlot at `atIndex` on every keyboard slot's track so
        bank slot counts stay in lockstep with SamplerInstrument::numPatterns_.
        Must be called from SamplerInstrument::insertPattern alongside the pad
        loop; otherwise `bank[i]` drifts from pattern index `i` after a
        pattern-insertion. */
    void insertPatternSlot(int atIndex);

    /** Flat note copy used by snapshot/write pair — mirrors the anonymous
        NoteCopy struct inside SamplerInstrument::clonePattern so the sampler
        can snapshot keyboard content before `insertPatternSlot` shifts slot
        indices, then replay it into the freshly inserted slot. */
    struct NoteCopy
    {
        int noteNumber;
        tracktion::BeatPosition start;
        tracktion::BeatDuration length;
        int velocity;
        int colour;
    };

    /** Capture each slot's content for pattern `srcIndex` into `out` (sized to
        getNumSlots()). Reads from the live MidiClip when `srcIndex` matches
        `activePatternIndex_`, otherwise from the slot's bank. Intended to be
        called BEFORE `insertPatternSlot` runs so bank indices still align
        with the caller's pre-shift `srcIndex`. */
    void snapshotPattern(int srcIndex,
                         std::vector<std::vector<NoteCopy>>& out) const;

    /** Replay `in` (per-slot notes captured via snapshotPattern) into
        `bank[destIndex]` on every slot. Called AFTER `insertPatternSlot` has
        opened the new slot. `in` may be shorter than `slots_` (extra slots
        noop); overflow entries past slots_.size() are ignored. */
    void writePattern(int destIndex,
                      const std::vector<std::vector<NoteCopy>>& in);

private:
    void ensureFolder();
    void ensureFolderPlugins();
    void rediscoverSlotsFromEdit();
    void removeSlotAt(int index);
    te::AudioTrack::Ptr createSlotTrack(int slotIndex);
    te::MidiClip::Ptr   ensureMidiClip(te::AudioTrack&);
    te::Plugin* installPlugin(te::AudioTrack&,
                              const juce::PluginDescription& desc);

    AudioEngine& engine_;
    te::Edit* edit_ { nullptr };
    te::FolderTrack::Ptr folder_;
    std::vector<Slot> slots_;
    int activeSlot_ { 0 };  // numSlots means empty cursor
    int activePatternIndex_ { 0 };
    // Mirrored onto freshly-created slot clips so late-added slots inherit
    // the current groove (same coupling model as SamplerInstrument).
    float swingStrength_ { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardInstrumentBank)
};
