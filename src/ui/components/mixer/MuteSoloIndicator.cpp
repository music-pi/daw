#include "MuteSoloIndicator.h"

#include "../../theme/UiTheme.h"
#include "../../UiRefresh.h"

void MuteSoloIndicator::setMuteState(bool isMuted)
{
    if (muteActive == isMuted)
        return;

    muteActive = isMuted;
    repaint();
    requestUiRefresh(*this);
}

void MuteSoloIndicator::setSoloState(bool isSolo)
{
    if (soloActive == isSolo)
        return;

    soloActive = isSolo;
    repaint();
    requestUiRefresh(*this);
}

void MuteSoloIndicator::setIndicatorEnabled(bool enabled)
{
    if (isEnabled == enabled)
        return;

    isEnabled = enabled;
    repaint();
    requestUiRefresh(*this);
}

void MuteSoloIndicator::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    const float corner = 8.0f;
    const float dividerInset = 4.0f;
    const float halfWidth = bounds.getWidth() * 0.5f;

    const auto baseColour = (isEnabled ? UiTheme::kIndicatorBase : UiTheme::kIndicatorBase.withAlpha(0.35f));
    const juce::Colour activeColour = isEnabled ? UiTheme::kIndicatorHighlight : UiTheme::kIndicatorHighlight.withAlpha(0.5f);
    const juce::Colour inactiveText = isEnabled ? UiTheme::kTextPrimary.withAlpha(0.85f) : UiTheme::kTextPrimary.withAlpha(0.4f);

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, corner);
    g.setColour(UiTheme::kStripBorder.withAlpha(isEnabled ? 0.45f : 0.25f));
    g.drawRoundedRectangle(bounds, corner, 1.1f);

    auto drawHalf = [&](bool isActive, bool isLeft) {
        if (!isActive)
            return;

        const float x = isLeft ? bounds.getX() : bounds.getX() + halfWidth;
        const float width = isLeft ? halfWidth : bounds.getWidth() - halfWidth;

        juce::Path path;
        path.addRoundedRectangle(x,
                                 bounds.getY(),
                                 width,
                                 bounds.getHeight(),
                                 corner,
                                 corner,
                                 isLeft,
                                 !isLeft,
                                 isLeft,
                                 !isLeft);

        g.setColour(activeColour);
        g.fillPath(path);
    };

    drawHalf(muteActive, true);
    drawHalf(soloActive, false);

    g.setColour(UiTheme::kStripBorder.withAlpha(isEnabled ? 0.4f : 0.2f));
    const float dividerX = bounds.getX() + halfWidth;
    g.drawLine(dividerX,
               bounds.getY() + dividerInset,
               dividerX,
               bounds.getBottom() - dividerInset,
               1.0f);

    auto textColourFor = [&](bool active) {
        if (!isEnabled)
            return inactiveText;
        return active ? juce::Colours::black.withAlpha(0.85f) : inactiveText;
    };

    juce::Rectangle<float> leftArea(bounds.getX(), bounds.getY(), halfWidth, bounds.getHeight());
    juce::Rectangle<float> rightArea(dividerX, bounds.getY(), bounds.getWidth() - halfWidth, bounds.getHeight());

    g.setColour(textColourFor(muteActive));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall, juce::Font::bold)));
    g.drawFittedText("MUTE", leftArea.reduced(6.0f).toNearestInt(), juce::Justification::centred, 1);

    g.setColour(textColourFor(soloActive));
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall, juce::Font::bold)));
    g.drawFittedText("SOLO", rightArea.reduced(6.0f).toNearestInt(), juce::Justification::centred, 1);
}

void MuteSoloIndicator::mouseUp(const juce::MouseEvent& event)
{
    if (!isEnabled)
        return;

    const bool isSoloRegion = event.position.x >= getWidth() * 0.5f;
    if (isSoloRegion)
    {
        const bool newState = !soloActive;
        if (onSoloToggle)
            onSoloToggle(newState);
        setSoloState(newState);
    }
    else
    {
        const bool newState = !muteActive;
        if (onMuteToggle)
            onMuteToggle(newState);
        setMuteState(newState);
    }
}
