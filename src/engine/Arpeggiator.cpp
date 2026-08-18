#include "Arpeggiator.h"

#include "AudioEngine.h"
#include "MidiConstants.h"

namespace
{
constexpr int kTimerHz = 200;   // 5ms tick granularity — fine for musical timing via injectLiveMidiMessage

juce::Identifier kStateNode { "arpeggiator" };

int modeToInt(Arpeggiator::Mode m) { return static_cast<int>(m); }
Arpeggiator::Mode intToMode(int v)
{
    const int clamped = juce::jlimit(0, (int) Arpeggiator::Mode::Chord, v);
    return static_cast<Arpeggiator::Mode>(clamped);
}

// Mutex-free helper: caller must hold Arpeggiator::mutex_.
void rebuildSortedFromHeld(const std::vector<std::pair<int,int>>& held,
                            std::vector<int>& out)
{
    out.clear();
    out.reserve(held.size());
    for (auto& [p, v] : held) out.push_back(p);
    std::sort(out.begin(), out.end());
}
}

Arpeggiator::Arpeggiator(AudioEngine& e) : engine_(e)
{
    // Timer starts on demand (when we have notes + a non-Off mode).
}

Arpeggiator::~Arpeggiator()
{
    stopTimer();
}

void Arpeggiator::setTrack(te::AudioTrack* track)
{
    // Take the stuck-note off work outside the lock — injectLiveMidiMessage
    // enqueues through TE's playback graph and can notify listeners; holding
    // mutex_ across the call is a re-entrancy / priority-inversion risk.
    te::AudioTrack* stuckOnTrack = nullptr;
    int stuckPitch = -1;
    int chan = 1;
    {
        std::lock_guard lk(mutex_);
        if (track_ == track) return;
        if (track_ != nullptr && lastEmittedPitch_ >= 0)
        {
            stuckOnTrack = track_;
            stuckPitch = lastEmittedPitch_;
            chan = settings_.channel;
            lastEmittedPitch_ = -1;
        }
        track_ = track;
    }

    if (stuckOnTrack != nullptr && stuckPitch >= 0)
        stuckOnTrack->injectLiveMidiMessage(
            juce::MidiMessage::noteOff(chan, stuckPitch), {});
}

void Arpeggiator::setSettings(Settings s)
{
    s.octaves = juce::jlimit(1, 4, s.octaves);
    s.rateBeats = juce::jmax(1.0 / 32.0, s.rateBeats);
    s.gate = juce::jlimit(0.05f, 1.0f, s.gate);
    s.channel = juce::jlimit(midi::kChannelMin, midi::kChannelMax, s.channel);

    bool becameOff = false;
    {
        std::lock_guard lk(mutex_);
        becameOff = (settings_.mode != Mode::Off && s.mode == Mode::Off);
        settings_ = s;
    }

    if (becameOff)
    {
        allNotesOff();
        stopTimer();
    }
    else
    {
        // If the mode just changed from Off, start the clock if notes are held.
        std::lock_guard lk(mutex_);
        if (settings_.mode != Mode::Off && ! heldOrdered_.empty() && ! isTimerRunning())
            startTimerHz(kTimerHz);
    }
}

Arpeggiator::Settings Arpeggiator::getSettings() const
{
    std::lock_guard lk(mutex_);
    return settings_;
}

void Arpeggiator::noteOn(int pitch, int velocity)
{
    std::lock_guard lk(mutex_);

    // Dedup: if already held, refresh velocity but don't duplicate.
    for (auto& entry : heldOrdered_)
        if (entry.first == pitch) { entry.second = velocity; return; }

    // Latch semantics: a fresh note-on after a latched release clears the
    // old set first, per Roland convention. We approximate this by noticing
    // when the set had gone to zero held but still contained latched entries.
    // For MVP, skip this nuance — adding a note to a latched set adds to it.

    heldOrdered_.emplace_back(pitch, velocity);
    rebuildSortedFromHeld(heldOrdered_, sortedHeld_);

    if (settings_.mode != Mode::Off && ! isTimerRunning())
    {
        // Reset step and beat so the first emission is immediate.
        step_ = 0;
        waitingForFirstPlayingStep_ = true;
        lastObservedBeats_ = -1.0;
        freeBeats_ = 0.0;
        startTimerHz(kTimerHz);
    }
}

void Arpeggiator::noteOff(int pitch)
{
    // See setTrack: never hold mutex_ across injectLiveMidiMessage.
    te::AudioTrack* flushOnTrack = nullptr;
    int flushPitch = -1;
    int chan = 1;
    {
        std::lock_guard lk(mutex_);
        if (settings_.latch) return;

        heldOrdered_.erase(
            std::remove_if(heldOrdered_.begin(), heldOrdered_.end(),
                           [pitch](const auto& e) { return e.first == pitch; }),
            heldOrdered_.end());
        rebuildSortedFromHeld(heldOrdered_, sortedHeld_);

        if (heldOrdered_.empty())
        {
            if (track_ != nullptr && lastEmittedPitch_ >= 0)
            {
                flushOnTrack = track_;
                flushPitch = lastEmittedPitch_;
                chan = settings_.channel;
                lastEmittedPitch_ = -1;
            }
            stopTimer();
        }
    }

    if (flushOnTrack != nullptr && flushPitch >= 0)
        flushOnTrack->injectLiveMidiMessage(
            juce::MidiMessage::noteOff(chan, flushPitch), {});
}

void Arpeggiator::allNotesOff()
{
    te::AudioTrack* flushOnTrack = nullptr;
    int flushPitch = -1;
    int chan = 1;
    {
        std::lock_guard lk(mutex_);
        if (track_ != nullptr && lastEmittedPitch_ >= 0)
        {
            flushOnTrack = track_;
            flushPitch = lastEmittedPitch_;
            chan = settings_.channel;
            lastEmittedPitch_ = -1;
        }
        heldOrdered_.clear();
        sortedHeld_.clear();
        stopTimer();
    }

    if (flushOnTrack != nullptr && flushPitch >= 0)
        flushOnTrack->injectLiveMidiMessage(
            juce::MidiMessage::noteOff(chan, flushPitch), {});
}

int Arpeggiator::getHeldCount() const
{
    std::lock_guard lk(mutex_);
    return static_cast<int>(heldOrdered_.size());
}

// ── Step resolution (pure) ──────────────────────────────────────────────────

int Arpeggiator::stepPitch(Mode mode,
                            const std::vector<int>& held,
                            int step,
                            int octaves,
                            unsigned randomSeed)
{
    if (held.empty() || mode == Mode::Off) return -1;
    octaves = juce::jlimit(1, 4, octaves);
    const int N = static_cast<int>(held.size());
    const int total = N * octaves;

    switch (mode)
    {
        case Mode::Chord:
            // Chord mode plays all held notes at once — caller picks only the
            // representative pitch (we use the lowest); the emitStep path
            // knows to broadcast to every held pitch.
            return held.front();

        case Mode::Random:
        {
            juce::Random rng(static_cast<juce::int64>(randomSeed)
                             ^ static_cast<juce::int64>(step) * 0x9E3779B1LL);
            const int idx = rng.nextInt(total);
            const int octOffset = idx / N;
            const int base = held[idx % N];
            return base + 12 * octOffset;
        }

        case Mode::AsPlayed:
        {
            const int idx = ((step % total) + total) % total;
            const int octOffset = idx / N;
            return held[idx % N] + 12 * octOffset;
        }

        case Mode::Up:
        {
            auto sorted = held; std::sort(sorted.begin(), sorted.end());
            const int idx = ((step % total) + total) % total;
            const int octOffset = idx / N;
            return sorted[idx % N] + 12 * octOffset;
        }

        case Mode::Down:
        {
            auto sorted = held; std::sort(sorted.begin(), sorted.end());
            const int idx = ((step % total) + total) % total;
            const int octOffset = idx / N;
            return sorted[N - 1 - (idx % N)] + 12 * octOffset;
        }

        case Mode::UpDown:
        {
            auto sorted = held; std::sort(sorted.begin(), sorted.end());
            // Up-then-down without repeating extremes (Sequential convention):
            // cycle length = 2*N - 2 (for N >= 2), or N for N == 1.
            const int cycle = (N <= 1) ? N : (2 * N - 2);
            const int totalUp = cycle * octaves;
            if (totalUp <= 0) return sorted[0];
            const int idx = ((step % totalUp) + totalUp) % totalUp;
            const int octOffset = idx / cycle;
            int localIdx = idx % cycle;
            if (localIdx >= N)
                localIdx = 2 * N - 2 - localIdx;
            return sorted[localIdx] + 12 * octOffset;
        }

        case Mode::Off:
            return -1;
    }
    return -1;
}

int Arpeggiator::advanceForTest()
{
    std::lock_guard lk(mutex_);
    // AsPlayed reads press-order; every other mode reads the cached sort so
    // we don't re-sort on every step.
    std::vector<int> asPlayed;
    if (settings_.mode == Mode::AsPlayed)
    {
        asPlayed.reserve(heldOrdered_.size());
        for (auto& [p, v] : heldOrdered_) asPlayed.push_back(p);
    }
    const auto& heldForMode = (settings_.mode == Mode::AsPlayed) ? asPlayed : sortedHeld_;
    const int pitch = stepPitch(settings_.mode, heldForMode, step_,
                                settings_.octaves,
                                static_cast<unsigned>(step_));
    ++step_;
    return pitch;
}

int Arpeggiator::getStepForTest() const
{
    std::lock_guard lk(mutex_);
    return step_;
}

// ── Timer tick ──────────────────────────────────────────────────────────────

void Arpeggiator::timerCallback()
{
    // Gather timing state under the lock, decide what to do, release, then
    // emit MIDI without holding the lock (injectLiveMidiMessage may notify).
    double stepRateBeats = 0.25;
    bool shouldEmit = false;
    bool shouldGateOff = false;
    int gateOffPitch = -1;
    int gateOffChannel = 1;
    te::AudioTrack* gateOffTrack = nullptr;
    {
        std::lock_guard lk(mutex_);
        if (settings_.mode == Mode::Off || heldOrdered_.empty())
        {
            stopTimer();
            return;
        }
        stepRateBeats = settings_.rateBeats;

        auto* edit = engine_.getEdit();
        const bool playing = engine_.isPlaying() && edit != nullptr;

        double currentBeats = 0.0;
        if (playing)
        {
            const auto tpos = edit->getTransport().getPosition();
            currentBeats = edit->tempoSequence.toBeats(tpos).inBeats();

            // Loop-restart detection: transport jumped backwards between
            // ticks (loop boundary, user seek, whatever). Re-align nextStep
            // to the new position's next grid boundary so we keep firing.
            const bool wrappedBackwards =
                lastObservedBeats_ >= 0.0
                && currentBeats + stepRateBeats * 0.5 < lastObservedBeats_;

            if (waitingForFirstPlayingStep_ || wrappedBackwards)
            {
                nextStepBeats_ = std::floor(currentBeats / stepRateBeats) * stepRateBeats
                               + stepRateBeats;
                waitingForFirstPlayingStep_ = false;
            }
            lastObservedBeats_ = currentBeats;

            if (currentBeats >= nextStepBeats_)
            {
                shouldEmit = true;
                nextStepBeats_ += stepRateBeats;
            }
        }
        else
        {
            const double bpm = (edit != nullptr)
                ? edit->tempoSequence.getTempoAt(tracktion::core::TimePosition{}).getBpm()
                : 120.0;
            const double beatsPerTick = (bpm / 60.0) / static_cast<double>(kTimerHz);
            freeBeats_ += beatsPerTick;
            currentBeats = freeBeats_;
            if (freeBeats_ >= stepRateBeats)
            {
                shouldEmit = true;
                freeBeats_ -= stepRateBeats;
                // Re-align currentBeats to "just past the step boundary" so the
                // about-to-be-scheduled gate-off beat uses the same origin.
                currentBeats = 0.0;
            }
        }

        // Gate expiry: if the currently-held arp note is past its gate-off
        // beat, schedule the note-off now. Track separately from step
        // emission so the sustain control is independent of step spacing.
        if (! shouldEmit
            && lastEmittedPitch_ >= 0
            && gateOffAtBeats_ >= 0.0
            && currentBeats >= gateOffAtBeats_)
        {
            shouldGateOff = true;
            gateOffPitch = lastEmittedPitch_;
            gateOffChannel = settings_.channel;
            lastEmittedPitch_ = -1;
            gateOffAtBeats_ = -1.0;
        }
        gateOffTrack = track_;
    }

    if (shouldGateOff && gateOffTrack != nullptr)
        gateOffTrack->injectLiveMidiMessage(
            juce::MidiMessage::noteOff(gateOffChannel, gateOffPitch), {});

    if (shouldEmit) emitStep();
}

void Arpeggiator::emitStep()
{
    // Collect step work under the lock + release before MIDI injection.
    int noteOffPitch = -1;
    int chan = 1;
    std::vector<std::pair<int,int>> chordNotes;          // {pitch, velocity}
    int monoPitch = -1;
    int monoVel = 96;
    te::AudioTrack* track = nullptr;
    {
        std::lock_guard lk(mutex_);
        if (track_ == nullptr || heldOrdered_.empty()) return;
        track = track_;

        chan = settings_.channel;

        if (lastEmittedPitch_ >= 0)
        {
            noteOffPitch = lastEmittedPitch_;
            lastEmittedPitch_ = -1;
        }

        if (settings_.mode == Mode::Chord)
        {
            chordNotes = heldOrdered_;
            lastEmittedPitch_ = heldOrdered_.front().first;
            lastEmittedVelocity_ = heldOrdered_.front().second;
        }
        else
        {
            // AsPlayed reads press-order; everything else uses the cached sort.
            std::vector<int> asPlayed;
            if (settings_.mode == Mode::AsPlayed)
            {
                asPlayed.reserve(heldOrdered_.size());
                for (auto& [p, v] : heldOrdered_) asPlayed.push_back(p);
            }
            const auto& heldForMode = (settings_.mode == Mode::AsPlayed) ? asPlayed : sortedHeld_;

            monoPitch = stepPitch(settings_.mode, heldForMode, step_,
                                  settings_.octaves,
                                  static_cast<unsigned>(step_));
            if (monoPitch >= 0 && monoPitch <= 127)
            {
                monoVel = 96;
                for (auto& [p, v] : heldOrdered_)
                    if ((monoPitch - p) % 12 == 0) { monoVel = v; break; }
                lastEmittedPitch_ = monoPitch;
                lastEmittedVelocity_ = monoVel;
            }
            else
            {
                monoPitch = -1;
            }
        }

        // Schedule gate-off for this step. beatsOrigin = 0 for free-run; for
        // transport-playing we use absolute beats, and gateOffAt is absolute
        // too. nextStepBeats_ was already advanced by the timer callback, so
        // subtracting rateBeats gives this step's start.
        auto* edit = engine_.getEdit();
        const bool playing = engine_.isPlaying() && edit != nullptr;
        const double stepStart = playing
            ? (nextStepBeats_ - settings_.rateBeats)
            : 0.0;
        gateOffAtBeats_ = stepStart + settings_.rateBeats * settings_.gate;
        ++step_;
    }

    if (track == nullptr) return;
    if (noteOffPitch >= 0)
        track->injectLiveMidiMessage(
            juce::MidiMessage::noteOff(chan, noteOffPitch), {});

    // Snapshot the callback + step length so we notify outside the mutex.
    // Length is gate-scaled rate (matches gateOffAtBeats_ computation above)
    // so a recording consumer sees the same duration the user hears.
    NoteEmittedCallback cb;
    double noteLen = 0.0;
    {
        std::lock_guard lk(mutex_);
        cb = noteEmittedCallback_;
        noteLen = juce::jmax(1.0 / 64.0, settings_.rateBeats * settings_.gate);
    }

    if (! chordNotes.empty())
    {
        for (auto& [p, v] : chordNotes)
        {
            track->injectLiveMidiMessage(
                juce::MidiMessage::noteOn(chan, p, static_cast<juce::uint8>(v)),
                {});
            if (cb) cb(p, v, noteLen);
        }
    }
    else if (monoPitch >= 0)
    {
        track->injectLiveMidiMessage(
            juce::MidiMessage::noteOn(chan, monoPitch,
                                      static_cast<juce::uint8>(monoVel)),
            {});
        if (cb) cb(monoPitch, monoVel, noteLen);
    }
}

void Arpeggiator::setNoteEmittedCallback(NoteEmittedCallback cb)
{
    std::lock_guard lk(mutex_);
    noteEmittedCallback_ = std::move(cb);
}

// ── Serialisation ───────────────────────────────────────────────────────────

juce::ValueTree Arpeggiator::toState() const
{
    std::lock_guard lk(mutex_);
    juce::ValueTree v(kStateNode);
    v.setProperty("mode",      modeToInt(settings_.mode), nullptr);
    v.setProperty("rateBeats", settings_.rateBeats, nullptr);
    v.setProperty("octaves",   settings_.octaves, nullptr);
    v.setProperty("gate",      (double) settings_.gate, nullptr);
    v.setProperty("latch",     settings_.latch, nullptr);
    v.setProperty("channel",   settings_.channel, nullptr);
    return v;
}

void Arpeggiator::restoreFromState(const juce::ValueTree& v)
{
    if (! v.hasType(kStateNode)) return;
    Settings s;
    s.mode      = intToMode(static_cast<int>(v.getProperty("mode", 0)));
    s.rateBeats = static_cast<double>(v.getProperty("rateBeats", 0.25));
    s.octaves   = static_cast<int>(v.getProperty("octaves", 1));
    s.gate      = static_cast<float>((double) v.getProperty("gate", 0.5));
    s.latch     = static_cast<bool>(v.getProperty("latch", false));
    s.channel   = static_cast<int>(v.getProperty("channel", 1));
    setSettings(s);
}
