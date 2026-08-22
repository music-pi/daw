#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>
#include "PadPatternBank.h"
#include "../control/HardwareConstants.h"

namespace te = tracktion::engine;

class AudioEngine;
class RoundRobinMidiPlugin;

/**
 * SamplerInstrument - Thin wrapper coordinating pad-to-track mapping
 *
 * This class does NOT do any audio processing. It coordinates:
 * - Mapping hardware pads (0-15) to AudioTracks
 * - Per-pad MidiClip pattern management (one MidiClip per pad holds the
 *   active pattern; non-active patterns live in a per-pad ClipSlot bank)
 * - Choke group behavior (live triggers via SamplerPlugin::allNotesOff)
 * - Delegating gain/level to TE plugins
 *
 * Track structure (managed by TE, not this class):
 *   FolderTrack "Sampler"
 *   ├── AudioTrack "Pad 1" (SamplerPlugin + VolumeAndPanPlugin + LevelMeterPlugin
 *   │                       + MidiClip holding the active pattern)
 *   ├── AudioTrack "Pad 2"
 *   └── ... (up to 16)
 *
 * Playback: every pad is driven by TE's audio-thread renderer from its own
 * MidiClip through its own SamplerPlugin. Per-pad plugin chains (EQ, sends,
 * volume) apply uniformly to manual triggers and sequenced playback.
 */
class SamplerInstrument : private juce::Timer
{
public:
    // 16 pads matches the MK3 hardware grid — single source of truth in
    // HardwareConstants::kPadCount.
    static constexpr int kMaxPads = HardwareConstants::kPadCount;
    static constexpr int kBaseMidiNote = 36;  // C2
    static constexpr int kMaxSampleLayers = 4;

    enum class TriggerMode
    {
        HoldEnvelope = 0,
        OneShot,
        RoundRobinOrdered,
        RoundRobinRandom,
        VelocityRoundRobinOrdered,
        VelocityRoundRobinRandom
    };

    enum class LayerVelocityCurve
    {
        Linear = 0,
        Soft,
        Hard,
        Fixed
    };

    struct SampleLayer
    {
        juce::File file;
        float gainDb { 0.0f };
        float randomWeight { 1.0f };
        LayerVelocityCurve velocityCurve { LayerVelocityCurve::Linear };
        float velocityMinimum { 0.85f };
        float velocityMaximum { 1.0f };
    };

    struct PadInfo
    {
        // --- Hot pointers first: touched on every trigger / audio-path query.
        // Keeping them at the front of the struct puts the fields the audio
        // thread cares about inside a single cache line.
        te::AudioTrack* track { nullptr };   // NOT owned - TE owns tracks
        te::SamplerPlugin* sampler { nullptr };
        RoundRobinMidiPlugin* roundRobinMidi { nullptr };
        te::ExternalPlugin* instrument { nullptr };
        te::VolumeAndPanPlugin* volume { nullptr };
        te::LevelMeterPlugin* meter { nullptr };

        // --- Owning / ref-counted pointers (8 bytes each on typical ABIs).
        te::MidiClip::Ptr patternClip;       // One MidiClip per pad holding the active pattern
        mutable std::unique_ptr<te::LevelMeasurer::Client> meterClient;
        PadPatternBank patternBank;          // Single pointer member inside

        // --- Larger aggregates.
        // Materialized song-timeline clips. Populated only in Track mode while
        // playback is staged — one entry per SongBlock across every lane,
        // sitting on pad.track at the block's bar range. Built by
        // materializeSongTimeline(), torn down by dematerializeSongTimeline().
        std::vector<te::MidiClip::Ptr> materializedSlotClips;
        std::vector<SampleLayer> sampleLayers;

        // --- 8-byte scalars grouped together.
        double rangeStartSeconds { 0.0 };
        double rangeEndSeconds { 0.0 };      // 0 = use full sample
        double cachedSampleRate { 0.0 };
        int64_t cachedLengthInSamples { 0 };
        double cachedLengthSeconds { 0.0 };

        // --- 4-byte scalars + bool tucked in here so the trailing pad
        // between the last 4-byte field and the following 8-byte field
        // absorbs the bool for free.
        int index { -1 };                    // 0-15 pad index
        int midiNote { 0 };
        int chokeGroup { 0 };                // -1 = self, 0 = none, 1-8 = groups
        TriggerMode triggerMode { TriggerMode::HoldEnvelope };
        float normalizationGainDb { 0.0f };
        bool hasSample { false };            // Co-located with the int/float block.

        // --- juce::String / juce::File are single-pointer (8 bytes) but cold.
        juce::String name;
        juce::File sampleFile;
    };

    // Serializable snapshot of pad state for UI consumers and group persistence
    struct PadSnapshot
    {
        struct PatternSnapshot
        {
            int patternIndex { -1 };
            int channelIndex { -1 };
            juce::BigInteger steps;
            std::vector<uint8_t> velocities; // MIDI 0-127, indexed by step
        };

        struct LayerSnapshot
        {
            juce::String name;
            float gainDb { 0.0f };
            float randomWeight { 1.0f };
            LayerVelocityCurve velocityCurve { LayerVelocityCurve::Linear };
            float velocityMinimum { 0.85f };
            float velocityMaximum { 1.0f };
        };

        int id { 0 };
        juce::String name;
        int midiChannel { 1 };
        int midiNote { 0 };
        std::vector<PatternSnapshot> patterns;
        bool hasSample { false };
        juce::String sampleName;
        juce::File sampleFile;
        double sampleRate { 44100.0 };
        int totalSamples { 0 };
        double totalLengthSeconds { 0.0 };
        int windowStartSample { 0 };
        int windowEndSample { 0 };
        double windowStartSeconds { 0.0 };
        double windowEndSeconds { 0.0 };
        int chokeGroup { 0 };
        TriggerMode triggerMode { TriggerMode::HoldEnvelope };
        int sampleLayerCount { 0 };
        std::vector<LayerSnapshot> sampleLayers;
        float gainDb { 0.0f };
        float levelRms { 0.0f };
        float levelPeakDbfs { -100.0f };
    };

    /** Selects the expensive, optional portions of PadSnapshot. Core identity,
        sample presence, and gain are always populated. A pure Levels request
        omits detailed state and sample-window metadata. Pattern snapshots
        allocate per-pad/per-pattern vectors; level reads consume Tracktion's
        meter-client accumulator. */
    enum class SnapshotContent : unsigned
    {
        State = 0,
        Patterns = 1,
        Levels = 2,
        All = 3
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void padTriggered(int padIndex) = 0;
        virtual void padStopped(int /*padIndex*/) {}
    };

    explicit SamplerInstrument(AudioEngine& engine, int groupIndex = 0);
    ~SamplerInstrument();

    void attachToEdit(te::Edit& edit);
    void detach();
    void removeFromEdit();
    [[nodiscard]] int getGroupIndex() const noexcept { return groupIndex_; }

    // Pad management
    int addPad(const juce::String& name = {});
    bool loadSample(int padIndex, const juce::File& sampleFile);
    void clearSample(int padIndex);

    /** Raw variants: same observable effect as loadSample/clearSample/etc.
        but mutate the SamplerPlugin ValueTree with a null UndoManager so
        they are safe to call from inside UndoableAction::perform()/undo().
        The standard variants re-enter the current UndoManager via TE's
        SamplerPlugin::add/removeSound/setSoundExcerpt/setSoundGains and
        will livelock (or silently no-op) during um.undo(). */
    bool loadSampleRaw(int padIndex, const juce::File& sampleFile);
    bool addSampleLayer(int padIndex, const juce::File& sampleFile);
    bool addSampleLayerRaw(int padIndex, const juce::File& sampleFile);
    bool removeLastSampleLayer(int padIndex);
    void clearSampleRaw(int padIndex);
    bool setPadSampleRangeRaw(int padId, double startSeconds, double endSeconds);
    bool setPadSampleNormalizationGainDbRaw(int padId, float gainDb);
    [[nodiscard]] int getPadCount() const noexcept { return static_cast<int>(pads_.size()); }
    const PadInfo* getPad(int padIndex) const;
    PadInfo* getPad(int padIndex);

    // Instrument plugin (replaces SamplerPlugin on the pad's track)
    bool hasInstrument(int padIndex) const;
    te::ExternalPlugin* getPadInstrument(int padIndex) const;
    bool setPadInstrumentRaw(int padIndex, const juce::PluginDescription& desc);
    bool clearPadInstrumentRaw(int padIndex);

    // Triggering
    void trigger(int padIndex, float velocity = 1.0f);
    /** Handle the physical release edge. Hold/Envelope stops the voice;
        One-shot and round-robin modes deliberately keep playing. */
    void releasePad(int padIndex);
    /** Audition one concrete sample layer without advancing the pad's
        ordered/random round-robin cursor. */
    bool triggerSampleLayer(int padIndex, int layerIndex, float velocity = 1.0f);
    void releaseSampleLayer(int padIndex, int layerIndex);
    void stopPad(int padIndex);
    void stopAll();

    // Gain (delegates to VolumeAndPanPlugin)
    void setGainDb(int padIndex, float db);
    void setGainDbRaw(int padIndex, float db);
    float getGainDb(int padIndex) const;

    // Mute / solo (delegate to pad's AudioTrack)
    void setMute(int padIndex, bool mute);
    bool isMuted(int padIndex) const;
    void setSolo(int padIndex, bool solo);
    bool isSoloed(int padIndex) const;

    // Levels (reads from LevelMeterPlugin)
    float getLevelDb(int padIndex) const;

    // Choke groups
    void setChokeGroup(int padIndex, int group);      // Creates undo transaction
    void setChokeGroupDirect(int padIndex, int group); // Direct set for command use
    int getChokeGroup(int padIndex) const;

    // Trigger modes and round-robin sample layers
    void setTriggerMode(int padIndex, TriggerMode mode);
    void setTriggerModeDirect(int padIndex, TriggerMode mode);
    TriggerMode getTriggerMode(int padIndex) const;
    int getSampleLayerCount(int padIndex) const;
    bool setSampleLayerGainDb(int padIndex, int layerIndex, float gainDb);
    bool setSampleLayerRandomWeight(int padIndex, int layerIndex, float weight);
    bool setSampleLayerVelocityCurve(int padIndex, int layerIndex,
                                     LayerVelocityCurve curve);
    bool setSampleLayerVelocityRange(int padIndex, int layerIndex,
                                     float minimum, float maximum);
    bool setSampleLayerGainDbRaw(int padIndex, int layerIndex, float gainDb);
    bool setSampleLayerRandomWeightRaw(int padIndex, int layerIndex, float weight);
    bool setSampleLayerVelocityCurveRaw(int padIndex, int layerIndex,
                                        LayerVelocityCurve curve);
    bool setSampleLayerVelocityRangeRaw(int padIndex, int layerIndex,
                                        float minimum, float maximum);
    static juce::String triggerModeName(TriggerMode);
    static juce::String velocityCurveName(LayerVelocityCurve);

    // Selection
    void selectPad(int padIndex);
    [[nodiscard]] int getSelectedPad() const noexcept { return selectedPad_; }

    // Pattern sequencing
    [[nodiscard]] int getNumPatterns() const noexcept { return numPatterns_; }
    int createPattern(const juce::String& name = {});
    bool setStep(int patternIndex, int padIndex, int step, bool enabled);
    bool setStepVelocity(int patternIndex, int padIndex, int step, int velocity127);
    std::optional<bool> toggleStep(int patternIndex, int padIndex, int step);  // Returns new state
    int getStepCount() const;
    double getPlayheadStep() const;

    /** Which pattern index is being played/edited. Callback-triggered pad
        sequencing reads from this pattern. Clamped to [0, numPatterns-1]. */
    [[nodiscard]] int getActivePatternIndex() const noexcept { return activePatternIndex_; }
    void setActivePatternIndex(int index);

    /** Human-readable name for a pattern index. Falls back to the default
        "Pattern N" (1-based) when no name has been set or the index is out
        of range. */
    juce::String getPatternName(int patternIndex) const;

    /** Store a user-provided name for a pattern index. Leading/trailing
        whitespace is trimmed; an empty string resets to the default. */
    void setPatternName(int patternIndex, const juce::String& name);

    /** Default "Pattern N" label (1-based) for the given index. Exposed so
        callers that want an explicit default (vs the stored-or-default from
        getPatternName) can use the same formatting. */
    juce::String getDefaultPatternName(int patternIndex) const;

    /** Human-readable name for the pad. Returns the stored pad.name, or
        empty string if the pad index is out of range. */
    juce::String getPadName(int padIndex) const;

    /** Store a user-provided name for a pad. Leading/trailing whitespace is
        trimmed; an empty string resets to the default "Pad N" label. Also
        mirrors the name onto the underlying TE AudioTrack so the rest of the
        engine's track-label paths stay in sync (matches loadSample/clearSample). */
    void setPadName(int padIndex, const juce::String& name);

    /** Clear every step (all channels) of the given pattern. Returns true on success. */
    bool clearPattern(int patternIndex);

    /** Delete the given pattern. If it's the last pattern, clears it in place
        instead (step clips must contain at least one pattern). Returns true on success. */
    bool deletePattern(int patternIndex);

    /** Resize the step-clip length (bars) and the pattern's note count
        (bars * stepsPerBar) atomically so playback actually spans all steps.
        Returns true on success. */
    bool setPatternLength(int patternIndex, int bars, int stepsPerBar);

    // Current pattern grid — keyboard slots mirror these for materialisation
    // and live-clip length so both subsystems stay in lockstep.
    [[nodiscard]] int getBarsPerPattern() const noexcept { return bars_; }
    [[nodiscard]] int getStepsPerBar() const noexcept { return stepsPerBar_; }
    [[nodiscard]] float getSwingStrength() const noexcept { return swingStrength_; }

    // Track access for MixerWidget
    [[nodiscard]] te::FolderTrack* getFolder() const noexcept { return folder_.get(); }
    te::AudioTrack* getTrack(int padIndex) const;

    // Query methods
    std::vector<PadSnapshot> getPadsSnapshot(
        SnapshotContent content = SnapshotContent::All) const;

    // Sample manipulation
    bool setPadSampleRange(int padId, double startSeconds, double endSeconds);
    bool setPadSampleNormalizationGainDb(int padId, float gainDb);
    bool truncatePadSample(int padId, int64_t startSample, int64_t endSample);
    bool normalizePadSample(int padId, double targetDb);

    // State persistence
    // NOTE: non-const because the serialization path syncs live timeline
    // edits back into each pad's PatternBank (a write) before snapshotting.
    bool serializePadsToState(juce::ValueTree& padsState,
                              const juce::File& projectFile = {});
    void restorePadsFromState(const juce::ValueTree& padsState,
                              const juce::File& projectFile = {});

    // Swing — writes grooveStrength to every pad's live MidiClip.
    void applySwing(float strength);

    // Arranger/loop range helper — end time of the pattern span (for loop
    // setup and playback scheduling). Reads from pad 0's MidiClip since all
    // pad clips are length-synchronised via setPatternLength().
    std::optional<tracktion::core::TimeRange> getPatternEditTimeRange() const;

    // Resize each pad's live MidiClip to the native pattern length. Called
    // from the song-slot edit paths and setPlayMode; Track mode no longer
    // extends the live clip — timeline materialization replaces it.
    void refreshPatternClipRanges();

    // Build a MidiClip per song slot on each pad's timeline so TE plays the
    // song natively (sample-accurate bar boundaries, no mid-play mutations).
    // Park the live pad.patternClip past song end so it doesn't overlap.
    // Called by AudioEngine::play() in Track mode with a non-empty song.
    void materializeSongTimeline();

    // Reverse of materializeSongTimeline: remove every materialized clip and
    // restore pad.patternClip to the native [0, bars_) range so Pattern mode
    // and static Track mode both see the canonical live clip. Safe no-op if
    // nothing was materialized.
    void dematerializeSongTimeline();

    // Rewrite any materialized clips whose song block references patternIndex
    // using the pattern's current bank content. Called after setStep,
    // setStepVelocity, clearPattern on a mid-play Track-mode edit. Current
    // strategy: dematerialize + rematerialize when the edited pattern has any
    // materialisations. Best-effort — the audio thread may read one block
    // mid-update (same tradeoff as the old swap-no-restart path).
    void updateMaterializedClipsForPattern(int patternIndex);

    // ── Song mode — FL Studio-style multi-lane grid ──
    //
    // The song is a fixed number of lanes, each carrying a time-ordered list
    // of SongBlocks. A SongBlock references one of the existing patterns and
    // occupies a half-open bar range [startBar, startBar + bars). Blocks on
    // the same lane never overlap; blocks on different lanes freely overlap
    // (their MIDI is summed by TE at materialize time — "Option A").
    struct SongBlock
    {
        int patternIndex { 0 };
        int startBar { 0 };
        int bars { 1 };
    };

    struct SongLane
    {
        std::vector<SongBlock> blocks; // sorted by startBar, no overlap
    };

    [[nodiscard]] const std::vector<SongLane>& getSongLanes() const noexcept { return songLanes_; }
    [[nodiscard]] int getNumSongLanes() const noexcept { return static_cast<int>(songLanes_.size()); }

    /** Total bars across the whole song: max(startBar + bars) over every
        block on every lane. Returns 0 for an empty song. */
    int getSongTotalBars() const;

    /** Inserts a block on `laneIndex` starting at `startBar` for `bars` bars
        (bars ≥ 1). Returns the new block's index within the lane, or -1 if
        the range overlaps an existing block on that lane, the lane index is
        out of range, or inputs are invalid. */
    int insertBlock(int laneIndex, int startBar, int patternIndex, int bars);

    /** Removes the block at (laneIndex, blockIndex). Returns false if the
        indices are out of range. */
    bool removeBlock(int laneIndex, int blockIndex);

    /** Changes a block's pattern reference. Returns false on invalid indices
        or out-of-range pattern index. */
    bool setBlockPattern(int laneIndex, int blockIndex, int patternIndex);

    /** Resizes a block in place. Returns false if the new length would
        overlap the next block on the lane, or if indices are invalid. */
    bool setBlockBars(int laneIndex, int blockIndex, int bars);

    /** Moves a block's start position within its lane. Re-sorts into position
        if needed. Returns false if the new position overlaps another block on
        the lane, or if indices are invalid. */
    bool setBlockStart(int laneIndex, int blockIndex, int startBar);

    /** Moves a block from (fromLane, fromBlock) to toLane at toStartBar,
        preserving patternIndex and bars. Returns false if the destination
        range overlaps an existing block on toLane, or on invalid indices. */
    bool moveBlockToLane(int fromLane, int fromBlock, int toLane, int toStartBar);

    /** Inserts a new empty lane at atIndex (clamped to [0, numLanes]).
        Returns the resolved insertion index. */
    int insertLane(int atIndex);

    /** Removes the lane at laneIndex. Refuses to remove the last remaining
        lane (the song always has ≥ 1 lane). Returns false on invalid index
        or last-lane removal attempt. */
    bool removeLane(int laneIndex);

    /** Inserts a new empty pattern at atIndex (clamped to [0, numPatterns]).
        Shifts the active-pattern index and every song-block pattern ref at or
        above the insertion point up by 1. Returns the resolved insertion index. */
    int insertPattern(int atIndex);

    /** Clones pattern srcIndex into a fresh pattern at srcIndex + 1. Returns
        the new pattern index, or -1 if srcIndex is invalid. */
    int clonePattern(int srcIndex);

    /** FL Studio "unique" on a song block: clones the block's current pattern
        and repoints this block at the clone. Other blocks that referenced a
        pattern index >= src's successor get shifted by insertPattern. Returns
        the new pattern index, or -1 on invalid (laneIndex, blockIndex). */
    int uniqueSongBlock(int laneIndex, int blockIndex);

    /** FL Studio "double" on a song block: clones the block's pattern AND
        inserts a fresh block immediately after the source on the same lane,
        referencing the clone. Returns the index of the newly-inserted block
        within its lane, or -1 on invalid (laneIndex, blockIndex) or if the
        destination range overlaps an existing block. On overlap the clone is
        kept (pattern count grows by 1) but no block is inserted. */
    int doubleSongBlock(int laneIndex, int blockIndex);

    // Listeners
    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

private:
    AudioEngine& engine_;
    int groupIndex_ { 0 };
    te::Edit* edit_ { nullptr };
    te::FolderTrack::Ptr folder_;

    void ensurePadPatternClip(PadInfo& pad);

    std::vector<PadInfo> pads_;
    int selectedPad_ { -1 };
    int activePatternIndex_ { 0 };
    int numPatterns_ { 1 };       // pattern-index range; each pad's ClipSlot bank tracks its own slots
    // One entry per pattern index. Parallel to numPatterns_: empty string =
    // "use the default Pattern N" so unnamed patterns cost nothing to store.
    std::vector<juce::String> patternNames_ { juce::String() };
    int stepsPerBar_ { 16 };
    int bars_ { 1 };
    // Fresh songs start with 5 empty lanes (UI default). detach() reseeds to
    // this same shape; restorePadsFromState overwrites it with whatever the
    // project file actually contains, so legacy saves keep their lane count.
    static constexpr int kDefaultSongLanes = 5;
    std::vector<SongLane> songLanes_ { static_cast<size_t>(kDefaultSongLanes), SongLane{} };
    float swingStrength_ { 0.0f }; // 0..1 — mirrored onto newly created MidiClips
    juce::ListenerList<Listener> listeners_;

    // UI feedback for pad trigger events (no audio triggering — TE handles that natively)
    int lastTriggeredStep_ { -1 };
    bool wasPlaying_ { false };

    // Meter reads are lock-free: te::LevelMeasurer::Client serializes
    // update/get internally with its own juce::SpinLock (see
    // tracktion_LevelMeasurer.cpp). Adding an outer mutex would only
    // make the message thread block on the audio thread for no gain.

    bool ensureInfrastructure();
    te::AudioTrack* createPadTrack(int padIndex, const juce::String& name);
    void restorePadTracksFromEdit();
    void setupTrackPlugins(PadInfo& pad);
    void applyChokeGroup(int triggeredPadIndex);

    // Pads bucketed by chokeGroup so applyChokeGroup visits only group members
    // rather than all 16 pads. Slot 0 is unused (group 0 = no-choke, handled
    // via early return); slots 1..8 each hold every pad index in that group.
    std::array<std::vector<int>, 9> chokeGroupIndex_ {};
    void rebuildChokeGroupIndex();
    void updateChokeGroupIndex(int padIndex, int oldGroup, int newGroup);
    void triggerPadTrack(PadInfo& pad, float velocity);
    void configurePadTriggering(PadInfo& pad, bool useUndoManager);
    void applyPadPlaybackProperties(PadInfo& pad);
    /** Raw mirror of applyPadPlaybackProperties — writes SOUND.startTime /
        length / gainDb / pan directly to the ValueTree with a null
        UndoManager. Used by raw APIs that may run inside um.undo(). */
    void applyPadPlaybackPropertiesRaw(PadInfo& pad);

    // Timer callback for UI feedback and choke-group pad-track triggering
    void timerCallback() override;
    void notifyPadTriggersForStep(int step);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerInstrument)
};
