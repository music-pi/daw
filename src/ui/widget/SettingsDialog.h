#pragma once

#include <functional>
#include <vector>

#include "Widget.h"

class SettingsDialog : public Widget
{
public:
    SettingsDialog();
    ~SettingsDialog() override = default;

    WidgetDescriptor describe() const override;
    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;
    void handleButton(const controller_events::ButtonEvent& e) override;

    void setDismissCallback(std::function<void()> callback);

    int getBlockSizeIndex() const { return blockSizeIndex_; }
    int getRefreshRateIndex() const { return refreshRateIndex_; }
    int getMeterSkewIndex() const { return meterSkewIndex_; }

private:
    void applyBlockSize();
    void applyRefreshRate();
    void applyMeterSkew();

    void paintGeneralPanel(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintUserPanel(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintKnobSlot(juce::Graphics& g, juce::Rectangle<int> bounds,
                       const juce::String& label, const juce::String& value);

    int blockSizeIndex_ = 0;
    int refreshRateIndex_ = 2;
    int meterSkewIndex_ = 1; // default = -12 dB (middle preset)
    std::array<int, 8> knobAccumulator_ {};
    static constexpr int kKnobThreshold = 12;
    static constexpr int kMeterSkewKnobThreshold = 32; // low sensitivity
    std::function<void()> onDismiss_;

    // 128 is the lowest setting that remains clean with MK3 audio and
    // unthrottled display traffic sharing the physical USB device.
    static constexpr std::array<int, 5> kBlockSizes = { 128, 256, 512, 1024, 2048 };
    static constexpr std::array<int, 5> kRefreshRates = { 20, 30, 60, 90, 120 };
    // Index 0 = less zoom (centre near 0), index 2 = more zoom (centre lower).
    static constexpr std::array<int, 3> kMeterSkewPresets = { -6, -12, -18 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
