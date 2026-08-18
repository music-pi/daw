#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace tracktion { inline namespace engine { class Engine; } }

/**
 * TransientDetector — wraps Tracktion's BeatDetect to produce slice start
 * times (in seconds) for an audio file. The returned vector always starts at
 * 0.0 so the first slice begins at the file origin; subsequent entries are
 * detected onsets. Capped at maxSlices via even downsampling.
 */
namespace TransientDetector
{
    /**
     * @param engine         Tracktion engine (for the audio format manager).
     * @param audioFile      Sample file to analyse.
     * @param sensitivity    0..1 — maps to a threshold multiplier; higher
     *                       = fewer, stronger onsets.
     * @param startSeconds   Start of the analysis window (absolute file
     *                       time). 0 = file origin.
     * @param endSeconds     End of the analysis window. <= startSeconds
     *                       means "up to EOF".
     * @param maxSlices      Hard cap on returned starts (default 16 — matches
     *                       the sampler pad count).
     * @return Sorted slice start times in **absolute file seconds**. Always
     *         begins with startSeconds. Empty if detection fails.
     */
    std::vector<double> detectSliceStarts(tracktion::engine::Engine& engine,
                                           const juce::File& audioFile,
                                           double sensitivity,
                                           double startSeconds = 0.0,
                                           double endSeconds = 0.0,
                                           int maxSlices = 16);
}
