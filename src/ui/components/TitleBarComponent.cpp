#include "TitleBarComponent.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"

TitleBarComponent::TitleBarComponent()
{
    setInterceptsMouseClicks(false, false);
}

void TitleBarComponent::setTitle(const juce::String& title)
{
    if (titleText == title)
        return;

    titleText = title;
    repaint();
    requestUiRefresh(*this);
}

void TitleBarComponent::setSubtitle(const juce::String& subtitle)
{
    if (subtitleText == subtitle && hasSubtitle)
        return;

    subtitleText = subtitle;
    hasSubtitle = subtitle.isNotEmpty();
    repaint();
    requestUiRefresh(*this);
}

void TitleBarComponent::clearSubtitle()
{
    if (!hasSubtitle)
        return;

    subtitleText = juce::String();
    hasSubtitle = false;
    repaint();
    requestUiRefresh(*this);
}

void TitleBarComponent::setAccentColour(const juce::Colour& colour)
{
    if (accentColour == colour)
        return;

    accentColour = colour;
    repaint();
    requestUiRefresh(*this);
}

void TitleBarComponent::setStatusIndicator(bool enabled, const juce::Colour& colour)
{
    if (indicatorVisible == enabled && (!enabled || indicatorColour == colour))
        return;

    indicatorVisible = enabled;
    indicatorColour = enabled ? colour : juce::Colours::transparentBlack;
    repaint();
    requestUiRefresh(*this);
}

void TitleBarComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    // Background
    g.setColour(UiTheme::kTitlebarBackground);
    g.fillRect(bounds);

    // Accent color square on the left
    if (accentColour != juce::Colours::transparentBlack)
    {
        g.setColour(accentColour);
        g.fillRect(bounds.removeFromLeft(UiTheme::kAccentWidth));
    }

    auto contentArea = bounds.reduced(UiTheme::kHorizontalPadding, UiTheme::kVerticalPadding);

    if (indicatorVisible && indicatorColour != juce::Colours::transparentBlack)
    {
        constexpr int indicatorDiameter = 10;
        auto indicatorArea = contentArea.removeFromRight(indicatorDiameter + UiTheme::kHorizontalPadding);
        auto circleBounds = indicatorArea.withSizeKeepingCentre(indicatorDiameter, indicatorDiameter);
        g.setColour(indicatorColour);
        g.fillEllipse(circleBounds.toFloat());
    }

    // Title (left-aligned)
    if (titleText.isNotEmpty())
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kTitlebarTitle, juce::Font::bold)));
        auto titleArea = contentArea.removeFromLeft(200);
        g.drawFittedText(titleText, titleArea, juce::Justification::centredLeft, 1);
    }

    // Subtitle (right-aligned)
    if (hasSubtitle && subtitleText.isNotEmpty())
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kTitlebarSubtitle)));
        g.drawFittedText(subtitleText, contentArea, juce::Justification::centredRight, 1);
    }
}

