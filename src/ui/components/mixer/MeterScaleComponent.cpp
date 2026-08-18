#include "MeterScaleComponent.h"

#include "../../theme/UiTheme.h"
#include "MixerUtils.h"

#include <cmath>

MeterScaleComponent::MeterScaleComponent()
{
    ticks = { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f, -42.0f, -48.0f, -54.0f, -60.0f };
}

void MeterScaleComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    g.setColour(UiTheme::kTextSecondary.withAlpha(0.65f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kScale, juce::Font::bold)));

    const float height = bounds.getHeight();
    for (float tick : ticks)
    {
        const float norm = MixerUtils::meterDbToNorm(tick);
        const float y = bounds.getBottom() - height * norm;

        g.setColour(UiTheme::kTextSecondary.withAlpha(0.25f));
        g.drawLine(bounds.getX(), y, bounds.getX() + 6.0f, y, 0.9f);
        g.setColour(UiTheme::kTextSecondary.withAlpha(0.7f));

        juce::String text;
        if (std::abs(tick) < 0.05f)
            text = "0";
        else
            text = juce::String(tick, 0);

        juce::Rectangle<int> labelBounds = juce::Rectangle<float>(bounds.getX() + 8.0f,
                                                                  y - 8.0f,
                                                                  bounds.getWidth() - 8.0f,
                                                                  16.0f)
                                               .toNearestInt();
        g.drawFittedText(text, labelBounds, juce::Justification::centredLeft, 1);
    }
}
