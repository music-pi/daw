#pragma once

#include "../../theme/UiTheme.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

struct MeterSmoother
{
    void configure(double updatesPerSecond, float attackSeconds, float releaseSeconds)
    {
        updateRate = juce::jmax(1.0, updatesPerSecond);
        attackAlpha = computeAlpha(attackSeconds);
        releaseAlpha = computeAlpha(releaseSeconds);
    }

    void reset(float value)
    {
        current = value;
    }

    float process(float target)
    {
        if (!std::isfinite(target))
            return current;

        const float alpha = (target > current) ? attackAlpha : releaseAlpha;
        current += alpha * (target - current);
        return current;
    }

private:
    double updateRate { 60.0 };
    float current { UiTheme::kMeterDbMin };
    float attackAlpha { 1.0f };
    float releaseAlpha { 1.0f };

    float computeAlpha(float seconds) const
    {
        if (seconds <= 0.0f)
            return 1.0f;

        const float steps = static_cast<float>(updateRate * seconds);
        if (steps <= 1.0f)
            return 1.0f;

        return juce::jlimit(0.01f, 1.0f, 1.0f / steps);
    }
};

namespace MixerUtils
{

inline double currentTimeSeconds()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

inline float sanitizeDb(float value, float floorDb)
{
    if (!std::isfinite(value))
        return floorDb;

    return value;
}

inline float dbToNorm(float db, float dbMin, float dbMax)
{
    db = juce::jlimit(dbMin, dbMax, db);
    return (db - dbMin) / (dbMax - dbMin);
}

namespace detail
{
    inline juce::NormalisableRange<float>& meterRange()
    {
        static juce::NormalisableRange<float> range = []() {
            juce::NormalisableRange<float> r { UiTheme::kMeterDbMin, UiTheme::kMeterDbMax };
            r.setSkewForCentre(-12.0f);
            return r;
        }();
        return range;
    }

    // Fader knob operates in the same skewed normalised space as the meter,
    // but spans the fader's full range (including the +6 dB boost).
    inline juce::NormalisableRange<float>& faderRange()
    {
        static juce::NormalisableRange<float> range = []() {
            juce::NormalisableRange<float> r { UiTheme::kFaderDbMin, UiTheme::kFaderDbMax };
            r.setSkewForCentre(-12.0f);
            return r;
        }();
        return range;
    }
}

// Nonlinear meter scale: the centre dB maps to the visual midpoint of the bar,
// giving more resolution near the top. Shared by LevelMeterComponent and
// MeterScaleComponent so zones and ticks follow whatever skew is set.
inline float meterDbToNorm(float db)
{
    db = juce::jlimit(UiTheme::kMeterDbMin, UiTheme::kMeterDbMax, db);
    return detail::meterRange().convertTo0to1(db);
}

// Step a fader dB value by `normDelta` along the shared skew. One step = a
// fixed fraction of the visible fader travel, so adjustments feel fine near
// 0 dB and coarse in the low-level tail.
inline float stepFaderDb(float currentDb, float normDelta)
{
    auto& range = detail::faderRange();
    const float currentNorm = range.convertTo0to1(juce::jlimit(UiTheme::kFaderDbMin,
                                                                UiTheme::kFaderDbMax,
                                                                currentDb));
    const float nextNorm = juce::jlimit(0.0f, 1.0f, currentNorm + normDelta);
    return range.convertFrom0to1(nextNorm);
}

// Position a fader visual (marker, scale ticks) on the same skewed curve
// the knob moves along, so they agree.
inline float faderDbToNorm(float db)
{
    db = juce::jlimit(UiTheme::kFaderDbMin, UiTheme::kFaderDbMax, db);
    return detail::faderRange().convertTo0to1(db);
}

inline void setMeterCentreDb(int centreDb)
{
    // setSkewForCentre would divide by zero at the endpoints.
    const int minCentre = static_cast<int>(UiTheme::kMeterDbMin) + 1;
    const int maxCentre = static_cast<int>(UiTheme::kMeterDbMax) - 1;
    centreDb = juce::jlimit(minCentre, maxCentre, centreDb);
    detail::meterRange().setSkewForCentre(static_cast<float>(centreDb));
    detail::faderRange().setSkewForCentre(static_cast<float>(centreDb));
}

} // namespace MixerUtils
