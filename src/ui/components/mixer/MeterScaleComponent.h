#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

class MeterScaleComponent : public juce::Component
{
public:
    MeterScaleComponent();
    void paint(juce::Graphics& g) override;

private:
    std::vector<float> ticks;
};
