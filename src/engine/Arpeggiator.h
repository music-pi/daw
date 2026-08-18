#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class AudioEngine;

/** Single-track held-note arpeggiator.

    Takes note-ons/note-offs (typically from a keyboard-mode pad handler)
    and emits timed note events into the active track's live-MIDI queue.
    Runs on a message-thread timer; MIDI goes out via
    te::AudioTrack::injectLiveMidiMessage which is safe from this thread.

    Sync policy: when TE's transport is playing, step advance is quantised
    to the transport beat grid; when stopped, the arp runs free off a local
    beat counter. Matches the "free when stopped, sync when playing"
    convention of Korg/Roland/Sequential hardware arps. */
class Arpeggiator : private juce::Timer
{
public:
    enum class Mode
    {
        Off,
        Up,
        Down,
        UpDown,
        Random,
        AsPlayed,
        Chord,
    };

    struct Settings
    {
        Mode mode { Mode::Off };
        double rateBeats { 0.25 };  // 16th-note = 0.25 beats
        int    octaves { 1 };       // 1..4
        float  gate { 0.5f };       // fraction of step
        bool   latch { false };
        int    channel { 1 };       // MIDI channel
    };

    explicit Arpeggiator(AudioEngine&);
    ~Arpeggiator() override;

    /** Owning track — emitted note events go here via injectLiveMidiMessage.
        Pass nullptr to detach; arp stops emitting immediately. */
    void setTrack(te::AudioTrack* track);

    void     setSettings(Settings);
    Settings getSettings() const;

    [[nodiscard]] bool isActive() const { return getSettings().mode != Mode::Off; }

    /** Transport events from the keyboard input layer. When latch is on,
        noteOff is a no-op unless the set was cleared via a modifier press;
        adding a new note to a latched set replaces it. */
    void noteOn(int pitch, int velocity);
    void noteOff(int pitch);
    void allNotesOff();

    // ── Serialisation ─────────────────────────────────────────────────────

    juce::ValueTree toState() const;
    void restoreFromState(const juce::ValueTree&);

    // ── Testing hooks ────────────────────────────────────────────────────

    /** Visible for tests: compute which pitch fires at a given step, given
        the current held set + settings. Pure — no timer, no MIDI. */
    static int stepPitch(Mode mode,
                         const std::vector<int>& heldOrdered,
                         int step,
                         int octaves,
                         unsigned randomSeed = 0xabc123u);

    /** Visible for tests: advance the internal step counter and return the
        pitch that would fire (without actually injecting MIDI). */
    int advanceForTest();

    /** Visible for tests: report the current step index (for observability). */
    int getStepForTest() const;

    /** Held-note count for UI display. */
    int getHeldCount() const;

    /** Fires on every note-on the arp emits, with the length that note will
        play for before gate-off. Use to mirror arp output into a MidiClip
        for record-mode capture. Called from the arp's timer on the message
        thread, outside the internal mutex. */
    using NoteEmittedCallback = std::function<void(int pitch, int velocity, double lengthBeats)>;
    void setNoteEmittedCallback(NoteEmittedCallback cb);

private:
    void timerCallback() override;
    void recomputeRate();
    void emitStep();

    AudioEngine& engine_;
    te::AudioTrack* track_ { nullptr };

    mutable std::mutex mutex_;
    Settings settings_;

    // Held notes: stored in press order for AsPlayed / Chord modes.
    std::vector<std::pair<int, int>> heldOrdered_;   // {pitch, velocity}

    // Sorted-pitch view of heldOrdered_, kept in sync on noteOn/noteOff so the
    // 200Hz timer path doesn't std::sort on every step (Up/Down/UpDown modes).
    std::vector<int> sortedHeld_;

    int  step_ { 0 };
    int  lastEmittedPitch_ { -1 };
    int  lastEmittedVelocity_ { 0 };
    double nextStepBeats_ { 0.0 };       // transport-relative beat where next step should fire
    double lastObservedBeats_ { -1.0 };  // previous tick's transport beat, to detect loop wraps
    double freeBeats_ { 0.0 };           // free-running counter when transport stopped
    double gateOffAtBeats_ { -1.0 };     // beat position at which the live note-on should gate off (-1 = no pending)
    bool   waitingForFirstPlayingStep_ { true };
    NoteEmittedCallback noteEmittedCallback_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Arpeggiator)
};
