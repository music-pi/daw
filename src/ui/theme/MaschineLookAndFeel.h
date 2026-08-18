#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UiTheme.h"

class MaschineLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MaschineLookAndFeel()
    {
        setColourScheme(getDarkColourScheme());
        setDefaultSansSerifTypefaceName("Inter");
    }

    void drawButtonBackground(juce::Graphics& g,
                             juce::Button& button,
                             const juce::Colour&,
                             bool isMouseOver,
                             bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);
        auto baseColour = UiTheme::kOptionFillEnabled;

        if (isButtonDown || button.getToggleState())
            baseColour = UiTheme::kOptionFillActive;
        else if (isMouseOver)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, UiTheme::kOptionCornerRadius);

        g.setColour(UiTheme::kOptionBorderEnabled);
        g.drawRoundedRectangle(bounds, UiTheme::kOptionCornerRadius, 1.0f);
    }

    void drawButtonText(juce::Graphics& g,
                       juce::TextButton& button,
                       bool isMouseOver,
                       bool isButtonDown) override
    {
        auto font = juce::Font(juce::FontOptions(UiTheme::Fonts::kOptionLabel, juce::Font::plain));
        g.setFont(font);

        auto textColour = (isButtonDown || button.getToggleState())
                             ? UiTheme::kOptionTextActive
                             : UiTheme::kOptionTextEnabled;

        if (!button.isEnabled())
            textColour = textColour.withAlpha(0.25f);

        g.setColour(textColour);
        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().reduced(UiTheme::kPadding),
                         juce::Justification::centred,
                         1);
    }

private:
    juce::LookAndFeel_V4::ColourScheme getDarkColourScheme()
    {
        // ColourScheme constructor requires all 9 colors in order:
        // windowBackground, widgetBackground, menuBackground, outline,
        // defaultText, defaultFill, highlightedText, highlightedFill, menuText
        return juce::LookAndFeel_V4::ColourScheme(
            UiTheme::kBackgroundDark,      // windowBackground
            UiTheme::kBackgroundDark,      // widgetBackground
            UiTheme::kBackgroundDark,      // menuBackground
            UiTheme::kOptionBorderEnabled,  // outline
            juce::Colours::white,           // defaultText
            UiTheme::kOptionFillEnabled,    // defaultFill
            juce::Colours::white,          // highlightedText
            UiTheme::kOptionFillActive,    // highlightedFill
            juce::Colours::white           // menuText
        );
    }
};

