#pragma once

#include <functional>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/** Per-pad pattern bank backed by a track's ClipSlotList. Each slot holds
    one MidiClip carrying a pattern's sequence; index N in the bank maps 1:1
    to slot index N on the track. TE owns the slots — the bank only owns the
    index policy. */
class PadPatternBank
{
public:
    /** Binds to an AudioTrack's ClipSlotList. Must outlive the track. */
    void attach (te::AudioTrack& track);

    /** Detaches from the current track. Safe to call multiple times. */
    void detach();

    /** Number of pattern slots currently allocated on the track. */
    [[nodiscard]] int getNumPatterns() const;

    /** Appends a new empty pattern slot. Returns its index. */
    int addPattern();

    /** Removes the slot at index. No-op if index is out of range. */
    void removePattern (int index);

    /** Overwrites the slot's MidiClip sequence with a copy of `list`. */
    void storePattern (int index, const te::MidiList& list);

    /** Copies the slot's MidiClip sequence into `dest`, replacing dest's
        contents. No-op if index is out of range (dest untouched). */
    void restorePattern (int index, te::MidiList& dest) const;

    /** Lets callers mutate the slot's MidiList in place. No-op if out of
        range. */
    void mutatePattern (int index, const std::function<void(te::MidiList&)>& fn);

    /** Read-only view of a slot's MidiList. No-op if out of range. */
    void readPattern (int index, const std::function<void(const te::MidiList&)>& fn) const;

private:
    te::AudioTrack* track_ { nullptr };
};
