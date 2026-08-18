#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widget.h"

/** Right-panel keyboard mode view. Page 0 is the chromatic pad-per-pitch
    map + slot nav; page 1 is the arpeggiator config (mode/rate/octaves/
    gate/latch). Owns pad LEDs p1..p16 on page 0 — C bright, naturals dim,
    accidentals off, with velocity-proportional green feedback while held. */
class KeyboardWidget : public Widget
{
public:
    KeyboardWidget() = default;
    ~KeyboardWidget() override = default;

    WidgetDescriptor describe() const override
    {
        return { "keyboard", 1, false, DisplayConstraint::RightOnly };
    }
    juce::String getTitle() const override { return "Keyboard"; }

    void onActivated(int panelOffset) override;
    void onDeactivated() override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int /*page*/) override { return {}; }
    std::unordered_map<int, uint8_t> getShiftPadOverlay(int page) override;
    void refreshPadLeds() override;

    void paint(juce::Graphics& g) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;

    // State injected by UiHost.
    void setBasePitch(int pitch);
    int getBasePitch() const { return basePitch_; }

    void setSlotInfo(int activeSlot, int numSlots, bool onEmptySlot,
                     juce::String activeName);
    void setVelocityMode(bool fixedVelocity, bool sixteenVelocities);

    /** Flash a held pad green with brightness derived from its emitted MIDI
        velocity. Release restores the current mode's layout colour. */
    void setPadFlashed(int padIndex, bool flashed, int midiVelocity = 0);

    // Callbacks — UiHost wires these to its KeyboardInstrumentBank helpers.
    void setOnPrevSlot(std::function<void()> cb)  { onPrev_  = std::move(cb); }
    void setOnNextSlot(std::function<void()> cb)  { onNext_  = std::move(cb); }
    void setOnAddInstrument(std::function<void()> cb)  { onAdd_    = std::move(cb); }
    void setOnLoadInstrument(std::function<void()> cb) { onLoad_   = std::move(cb); }
    void setOnDeleteSlot(std::function<void()> cb)     { onDelete_ = std::move(cb); }

private:
    void publishPadLeds();

    int basePitch_ { 36 };
    int activeSlot_ { 0 };
    int numSlots_ { 0 };
    bool onEmptySlot_ { true };
    bool fixedVelocity_ { false };
    bool sixteenVelocities_ { false };
    juce::String activeName_;

    std::function<void()> onPrev_;
    std::function<void()> onNext_;
    std::function<void()> onAdd_;
    std::function<void()> onLoad_;
    std::function<void()> onDelete_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardWidget)
};
