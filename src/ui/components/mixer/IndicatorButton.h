#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class IndicatorButton : public juce::TextButton
{
public:
    IndicatorButton(const juce::String& labelText, bool toggle);

    void setIndicatorState(bool activeState);
    void setBadgeText(juce::String text);

    void paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override;

private:
    bool toggleMode { false };
    bool active { false };
    juce::String badgeText;
};
