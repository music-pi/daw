#pragma once

#include "Widget.h"

/** System-level widget that owns transport LEDs. No visual panel
    (pageCount=0). LED refresh happens on Widget::onUiHostTick — the
    project's shared tick path, so no private juce::Timer here. */
class TransportWidget : public Widget
{
public:
    TransportWidget() = default;
    ~TransportWidget() override = default;

    WidgetDescriptor describe() const override;
    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    std::vector<std::string> requiredResources(int page) override;

    std::vector<Option> getOptions(int /*page*/) override { return {}; }
    std::vector<Knob> getKnobs(int /*page*/) override { return {}; }
    void paintPage(juce::Graphics&, int, juce::Rectangle<int>) override {}
    void paint(juce::Graphics&) override {}

    void handleButton(const controller_events::ButtonEvent& e) override;
    void onUiHostTick() override;

private:
    void updateLeds();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportWidget)
};
