#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Titlebar component matching Figma design
// Displays a title (left-aligned) and optional subtitle/info (right-aligned)
class TitleBarComponent : public juce::Component
{
public:
    TitleBarComponent();
    ~TitleBarComponent() override = default;

    void setTitle(const juce::String& title);
    void setSubtitle(const juce::String& subtitle);
    void clearSubtitle();
    void setAccentColour(const juce::Colour& colour);
    void setStatusIndicator(bool enabled, const juce::Colour& colour = juce::Colours::transparentBlack);
    
    juce::String getSubtitle() const { return subtitleText; }

    void paint(juce::Graphics& g) override;

private:
    juce::String titleText;
    juce::String subtitleText;
    bool hasSubtitle { false };
    juce::Colour accentColour { juce::Colours::transparentBlack };
    bool indicatorVisible { false };
    juce::Colour indicatorColour { juce::Colours::transparentBlack };
};

