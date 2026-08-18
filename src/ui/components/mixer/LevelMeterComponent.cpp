#include "LevelMeterComponent.h"

#include <cmath>

#include "../../theme/UiTheme.h"
#include "../../UiRefresh.h"
#include "MixerUtils.h"

void LevelMeterComponent::setLevels(float peakDbfs, float rmsDbfs, float peakHoldDbfs)
{
    const float newPeak = juce::jlimit(UiTheme::kMeterDbMin, UiTheme::kMeterDbMax,
                                       MixerUtils::sanitizeDb(peakDbfs, UiTheme::kMeterDbMin));
    const float newRms = juce::jlimit(UiTheme::kMeterDbMin, UiTheme::kMeterDbMax,
                                      MixerUtils::sanitizeDb(rmsDbfs, UiTheme::kMeterDbMin));
    const float newHold = juce::jlimit(UiTheme::kMeterDbMin, UiTheme::kMeterDbMax,
                                       MixerUtils::sanitizeDb(peakHoldDbfs, UiTheme::kMeterDbMin));

    // Skip the repaint when the displayed values haven't moved meaningfully —
    // keeps the dirty-gate quiet on silent channels (meters settled at floor)
    // and preserves the buffered-to-image cache on the parent strip.
    constexpr float kEpsilon = 0.05f;
    if (std::abs(newPeak - peak) < kEpsilon
        && std::abs(newRms - rms) < kEpsilon
        && std::abs(newHold - peakHold) < kEpsilon)
        return;

    peak = newPeak;
    rms = newRms;
    peakHold = newHold;
    repaint();
    requestUiRefresh(*this);
}

void LevelMeterComponent::setCompact(bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;

    compact = shouldBeCompact;
    repaint();
    requestUiRefresh(*this);
}

void LevelMeterComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    auto meterArea = bounds.reduced(2.0f);

    g.setColour(UiTheme::kMeterBackground);
    g.fillRect(meterArea);
    g.setColour(UiTheme::kStripBorder.withAlpha(0.45f));
    g.drawRect(meterArea, 1.0f);

    juce::ColourGradient backgroundGradient(juce::Colours::black.withAlpha(0.35f),
                                            meterArea.getX(), meterArea.getY(),
                                            juce::Colours::black.withAlpha(0.0f),
                                            meterArea.getX(), meterArea.getBottom(), false);
    g.setGradientFill(backgroundGradient);
    g.fillRect(meterArea);

    const float peakNorm = MixerUtils::meterDbToNorm(peak);
    const float rmsNorm = MixerUtils::meterDbToNorm(rms);
    const float height = meterArea.getHeight();

    auto drawZone = [&g, &meterArea, height](float zoneStart, float zoneEnd, juce::Colour colour, float levelNorm)
    {
        const float zoneStartNorm = MixerUtils::meterDbToNorm(zoneStart);
        const float zoneEndNorm = MixerUtils::meterDbToNorm(zoneEnd);
        const float topNorm = juce::jlimit(zoneStartNorm, zoneEndNorm, levelNorm);
        if (topNorm <= zoneStartNorm + 0.0001f)
            return;

        const float yTop = meterArea.getBottom() - height * topNorm;
        const float yBottom = meterArea.getBottom() - height * zoneStartNorm;
        if (yBottom <= yTop)
            return;

        juce::Rectangle<float> zone(meterArea.getX(), yTop, meterArea.getWidth(), yBottom - yTop);
        g.setColour(colour.withAlpha(0.92f));
        g.fillRect(zone);
    };

    drawZone(UiTheme::kMeterDbMin, -12.0f, UiTheme::kMeterGreen, peakNorm);
    drawZone(-12.0f, -3.0f, UiTheme::kMeterYellow, peakNorm);
    drawZone(-3.0f, UiTheme::kMeterDbMax, UiTheme::kMeterRed, peakNorm);

    const float rmsHeight = height * rmsNorm;
    if (rmsHeight > 0.0f)
    {
        juce::Rectangle<float> rmsRect(meterArea.getX() + meterArea.getWidth() * 0.12f,
                                       meterArea.getBottom() - rmsHeight,
                                       meterArea.getWidth() * 0.76f,
                                       rmsHeight);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRect(rmsRect);
    }

    // 0 dB notch.
    if (!compact)
    {
        const float unityY = meterArea.getBottom() - height * MixerUtils::meterDbToNorm(0.0f);
        g.setColour(UiTheme::kTextSecondary.withAlpha(0.4f));
        g.drawLine(meterArea.getX() + 2.0f, unityY, meterArea.getRight() - 2.0f, unityY, 1.0f);
    }

    // Peak hold marker.
    if (peakHold > UiTheme::kMeterDbMin + 0.2f)
    {
        const float peakHoldNorm = MixerUtils::meterDbToNorm(peakHold);
        const float peakHoldY = meterArea.getBottom() - height * peakHoldNorm;

        juce::Rectangle<float> holdMarker(meterArea.getX() + 1.5f,
                                          peakHoldY - 1.2f,
                                          meterArea.getWidth() - 3.0f,
                                          2.4f);

        g.setColour(UiTheme::kIndicatorHighlight.brighter(0.25f));
        g.fillRect(holdMarker);
    }
}
