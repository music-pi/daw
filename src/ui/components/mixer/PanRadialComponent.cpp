#include "PanRadialComponent.h"

#include "../../theme/UiTheme.h"
#include "../../UiRefresh.h"
#include "MixerUtils.h"

#include <cmath>

void PanRadialComponent::setPan(float newPan)
{
    const float clamped = juce::jlimit(-1.0f, 1.0f, MixerUtils::sanitizeDb(newPan, 0.0f));
    if (!juce::approximatelyEqual(pan, clamped))
    {
        pan = clamped;
        repaint();
        requestUiRefresh(*this);
    }
}

void PanRadialComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    const float labelHeight = 18.0f;
    auto labelBounds = bounds.removeFromBottom(labelHeight);
    auto circleBounds = bounds.reduced(6.0f);

    const float diameter = juce::jmin(circleBounds.getWidth(), circleBounds.getHeight());
    circleBounds = juce::Rectangle<float>(circleBounds.getCentreX() - diameter * 0.5f,
                                          circleBounds.getCentreY() - diameter * 0.5f,
                                          diameter,
                                          diameter);

    g.setColour(UiTheme::kIndicatorBase.withAlpha(0.75f));
    g.fillEllipse(circleBounds);
    g.setColour(UiTheme::kIndicatorHighlight.withAlpha(0.2f));
    g.drawEllipse(circleBounds, 1.6f);

    const float startAngle = -juce::MathConstants<float>::twoPi / 3.0f;
    const float endAngle = -juce::MathConstants<float>::pi / 3.0f;

    juce::Path arc;
    arc.addArc(circleBounds.getX(), circleBounds.getY(),
               circleBounds.getWidth(), circleBounds.getHeight(),
               startAngle, endAngle, true);

    g.setColour(UiTheme::kIndicatorHighlight.withAlpha(0.35f));
    g.strokePath(arc, juce::PathStrokeType(1.8f));

    const juce::Point<float> centre = circleBounds.getCentre();
    const float radius = circleBounds.getWidth() * 0.5f;
    const float angle = juce::jmap(pan, -1.0f, 1.0f, startAngle, endAngle);
    juce::Point<float> marker(centre.x + std::cos(angle) * (radius - 6.0f),
                              centre.y + std::sin(angle) * (radius - 6.0f));

    g.setColour(UiTheme::kIndicatorAccent);
    g.drawLine(centre.x, centre.y, marker.x, marker.y, 2.0f);

    const int panAmount = static_cast<int>(std::round(std::abs(pan) * 50.0f));
    juce::String panValue = panAmount == 0 ? juce::String("C")
                                           : juce::String(pan < 0.0f ? "L" : "R") + juce::String(panAmount);

    g.setColour(UiTheme::kTextPrimary.withAlpha(0.9f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSnackbar, juce::Font::bold)));
    g.drawFittedText(panValue, circleBounds.toNearestInt(), juce::Justification::centred, 1);

    g.setColour(UiTheme::kTextSecondary.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kScale, juce::Font::bold)));
    g.drawFittedText("PAN", labelBounds.toNearestInt(), juce::Justification::centredTop, 1);
}
