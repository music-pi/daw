#include "GainScaleComponent.h"

#include "../../theme/UiTheme.h"
#include "MixerUtils.h"

#include <cmath>

GainScaleComponent::GainScaleComponent()
{
    // Ticks live on the same skewed fader range as the knob and marker; -48
    // and +6 are the true fader endpoints.
    ticks = { -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f };
}

void GainScaleComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    g.setColour(UiTheme::kTextSecondary.withAlpha(0.65f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kScale, juce::Font::bold)));

    const float height = bounds.getHeight();
    for (float tick : ticks)
    {
        const float norm = MixerUtils::faderDbToNorm(tick);
        const float y = bounds.getBottom() - height * norm;

        g.setColour(UiTheme::kTextSecondary.withAlpha(0.25f));
        g.drawLine(bounds.getRight() - 6.0f, y, bounds.getRight(), y, 0.9f);
        g.setColour(UiTheme::kTextSecondary.withAlpha(0.7f));

        juce::String label;
        if (std::abs(tick) < 0.05f)
            label = "+0";
        else if (tick > 0.0f)
            label = "+" + juce::String(tick, 0);
        else
            label = juce::String(tick, 0);

        juce::Rectangle<int> labelBounds = juce::Rectangle<float>(bounds.getX(),
                                                                  y - 8.0f,
                                                                  bounds.getWidth() - 8.0f,
                                                                  16.0f)
                                               .toNearestInt();
        g.drawFittedText(label, labelBounds, juce::Justification::centredRight, 1);
    }
}
