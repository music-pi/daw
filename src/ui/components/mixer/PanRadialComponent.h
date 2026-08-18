#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PanRadialComponent : public juce::Component
{
public:
    void setPan(float newPan);
    void paint(juce::Graphics& g) override;

private:
    float pan { 0.0f };
};
