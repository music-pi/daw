#pragma once

/**
 * Named constants for MIDI value ranges. Replaces magic-number bounds at
 * jlimit/clamp sites so intent is clear and a future format bump (if any)
 * has one place to change.
 */
namespace midi
{
    // MIDI note numbers are 0..127 (C-1 to G9 in the General MIDI numbering).
    inline constexpr int kNoteMin     = 0;
    inline constexpr int kNoteMax     = 127;

    // Velocity: 0..127. Note-on with velocity 0 is a note-off by convention,
    // so most "live" emit paths clamp to [1, 127].
    inline constexpr int kVelocityMin = 0;
    inline constexpr int kVelocityMax = 127;
    inline constexpr int kVelocityLiveMin = 1;
    inline constexpr int kDefaultFixedVelocity = kVelocityMax;

    /** Quantise a pressure-derived live velocity to sixteen evenly-spaced
        levels. The highest level clamps to MIDI's maximum instead of 128. */
    constexpr int quantizeToSixteenVelocityLevels(int velocity) noexcept
    {
        if (velocity <= kVelocityLiveMin)
            return 8;
        const int value = ((velocity + 7) / 8) * 8;
        return value > kVelocityMax ? kVelocityMax : value;
    }

    // 1-based MIDI channel, 1..16.
    inline constexpr int kChannelMin  = 1;
    inline constexpr int kChannelMax  = 16;
}
