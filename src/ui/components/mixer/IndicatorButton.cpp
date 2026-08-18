#include "IndicatorButton.h"

#include "../../theme/UiTheme.h"
#include "../../UiRefresh.h"

IndicatorButton::IndicatorButton(const juce::String& labelText, bool toggle)
    : juce::TextButton(labelText)
    , toggleMode(toggle)
{
    setClickingTogglesState(toggle);
    setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.75f));
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void IndicatorButton::setIndicatorState(bool activeState)
{
    if (toggleMode)
    {
        if (getToggleState() != activeState)
            setToggleState(activeState, juce::dontSendNotification);
    }
    else if (active != activeState)
    {
        active = activeState;
        repaint();
        requestUiRefresh(*this);
    }
}

void IndicatorButton::setBadgeText(juce::String text)
{
    if (badgeText == text)
        return;

    badgeText = std::move(text);
    repaint();
    requestUiRefresh(*this);
}

void IndicatorButton::paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat();

    const bool highlighted = toggleMode ? getToggleState() : active;
    juce::Colour background = highlighted ? UiTheme::kIndicatorHighlight : UiTheme::kIndicatorBase;

    if (!isEnabled())
        background = background.withMultipliedAlpha(0.35f);
    else if (isMouseOverButton || isButtonDown)
        background = background.brighter(0.12f);

    g.setColour(background);
    g.fillRect(bounds);

    g.setColour(UiTheme::kStripBorder.withAlpha(0.45f));
    g.drawRect(bounds, 1.0f);

    g.setColour(highlighted ? juce::Colours::black.withAlpha(0.8f) : UiTheme::kTextPrimary.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kIndicator, juce::Font::bold)));

    juce::Rectangle<float> content = bounds.reduced(6.0f);
    if (badgeText.isNotEmpty())
    {
        juce::Rectangle<int> badgeArea = content.removeFromBottom(14.0f).toNearestInt();
        g.setColour(highlighted ? juce::Colours::black.withAlpha(0.75f) : UiTheme::kTextSecondary);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kIndicatorSmall, juce::Font::bold)));
        g.drawFittedText(badgeText, badgeArea, juce::Justification::centred, 1);
        g.setColour(highlighted ? juce::Colours::black.withAlpha(0.85f) : UiTheme::kTextPrimary.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kIndicator, juce::Font::bold)));
    }

    g.drawFittedText(getButtonText(), content.toNearestInt(), juce::Justification::centred, 2);
}
