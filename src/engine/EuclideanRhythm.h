#pragma once

#include <vector>

/** Euclidean rhythm generator.

    Distributes N beats as evenly as possible across K steps using
    Bjorklund's algorithm — the same algorithm used by Polyend Tracker,
    Elektron Octatrack (Arranger), Intellijel Euclidean Circles and the
    original G. Toussaint paper. Useful for quick polyrhythmic fills:
    e(3, 8) = Afro-Cuban tresillo, e(5, 8) = cinquillo, e(7, 16) = bossa. */
namespace EuclideanRhythm
{

/** Compute an N-of-K euclidean pattern with optional step rotation.

    @param pulses   Number of hits to distribute (0..steps). Clamped.
    @param steps    Total step count (>=1).
    @param rotation Rotation offset — shifts the result left by N positions.
                    Useful because Bjorklund always starts with a hit;
                    users may want to offset the downbeat.
    @returns        Vector of length `steps`; true means "hit on this step".
                    Empty when steps <= 0. */
std::vector<bool> pattern(int pulses, int steps, int rotation = 0);

}
