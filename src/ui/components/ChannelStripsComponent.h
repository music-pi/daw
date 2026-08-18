#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "mixer/ChannelDescriptor.h"
#include "mixer/MixerChannelState.h"

/**
 * ChannelStripsComponent — renders up to 4 full detailed channel strips
 * side-by-side on a single 480×272 panel. Each strip shows a level meter,
 * fader with scales, MUTE/PLGS/EQ/AUX indicator pills, and a pan radial.
 * The Active strip is highlighted with a slightly lighter background.
 */
class ChannelStripsComponent : public juce::Component
{
public:
    struct ChannelView
    {
        ChannelDescriptor descriptor;
        std::shared_ptr<MixerChannelState> state;
    };

    ChannelStripsComponent();
    ~ChannelStripsComponent() override;

    void setChannels(std::vector<ChannelView> channels);
    void setActiveIndex(int index);
    void setMuteModifierHeld(bool held);
    void setSoloModifierHeld(bool held);

    /** Pull live meter/fader/pan state into each strip. Call from the UiHost tick.
        Individual strip children only mark themselves dirty when their values
        actually change, so the dirty-gate stays quiet on idle/silent channels. */
    void refreshLiveState();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class ChannelStrip;

    void layoutContent();

    std::vector<ChannelView> channels_;
    int activeIndex_ { -1 };
    bool muteModifierHeld_ { false };
    bool soloModifierHeld_ { false };

    juce::OwnedArray<ChannelStrip> strips_;
};
