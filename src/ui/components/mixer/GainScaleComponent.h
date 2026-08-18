#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

class GainScaleComponent : public juce::Component
{
public:
    GainScaleComponent();
    void paint(juce::Graphics& g) override;

private:
    std::vector<float> ticks;
};
