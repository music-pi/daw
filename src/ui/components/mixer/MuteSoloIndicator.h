#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class MuteSoloIndicator : public juce::Component
{
public:
    std::function<void(bool)> onMuteToggle;
    std::function<void(bool)> onSoloToggle;

    void setMuteState(bool isMuted);
    void setSoloState(bool isSolo);
    void setIndicatorEnabled(bool enabled);

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    bool muteActive { false };
    bool soloActive { false };
    bool isEnabled { false };
};
