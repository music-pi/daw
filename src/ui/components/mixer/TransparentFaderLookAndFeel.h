#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class TransparentFaderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override
    {
        // Visual handled by LevelMeter + FaderMarker overlay.
    }
};
