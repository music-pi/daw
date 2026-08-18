#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class FaderMarkerOverlay : public juce::Component
{
public:
    void setGainDb(float gainDb);
    void paint(juce::Graphics& g) override;

private:
    float gain { 0.0f };
};
