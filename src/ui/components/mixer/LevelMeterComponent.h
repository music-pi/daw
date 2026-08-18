#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class LevelMeterComponent : public juce::Component
{
public:
    void setLevels(float peakDbfs, float rmsDbfs, float peakHoldDbfs);
    void setCompact(bool shouldBeCompact);
    void paint(juce::Graphics& g) override;

private:
    float peak { -60.0f };
    float rms { -60.0f };
    float peakHold { -60.0f };
    bool compact { false };
};
