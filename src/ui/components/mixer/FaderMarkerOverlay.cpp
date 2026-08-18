#include "FaderMarkerOverlay.h"

#include "../../theme/UiTheme.h"
#include "../../UiRefresh.h"
#include "MixerUtils.h"

void FaderMarkerOverlay::setGainDb(float gainDb)
{
    const float newGain = MixerUtils::sanitizeDb(gainDb, UiTheme::kFaderDbMin);
    if (juce::approximatelyEqual(gain, newGain))
        return;
    gain = newGain;
    repaint();
    requestUiRefresh(*this);
}

void FaderMarkerOverlay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    const float norm = MixerUtils::faderDbToNorm(gain);
    const float y = bounds.getBottom() - bounds.getHeight() * norm;

    g.setColour(UiTheme::kTextPrimary.withAlpha(0.9f));
    g.drawLine(bounds.getX() + 1.5f, y, bounds.getRight() - 1.5f, y, 1.6f);
    g.setColour(UiTheme::kIndicatorHighlight.withAlpha(0.9f));
    g.drawLine(bounds.getRight() - 1.5f, y - 6.0f, bounds.getRight() - 1.5f, y + 6.0f, 1.8f);
}
