#pragma once

#include <functional>

#include "Widget.h"

/** Modal configuration popover for the keyboard-mode arpeggiator.

    Opened via shift+noteRepeatArp. The button alone toggles the arp
    on/off; this dialog is only for tweaking rate/mode/octaves/gate/latch.

    All four right-panel knobs (k5..k8) map to Mode/Rate/Oct/Gate; d5..d8
    are On-Off / Latch / Close / (spare). */
class ArpDialog : public Widget
{
public:
    ArpDialog() = default;
    ~ArpDialog() override = default;

    WidgetDescriptor describe() const override
    {
        return { "arp_dialog", 1, true, DisplayConstraint::Any };
    }
    juce::String getTitle() const override { return "Arpeggiator"; }

    void onActivated(int panelOffset) override;
    void onDeactivated() override {}

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    void paint(juce::Graphics& g) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;

    void setDismissCallback(std::function<void()> cb) { onDismiss_ = std::move(cb); }

private:
    std::function<void()> onDismiss_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpDialog)
};
