#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../shared/ControlTypes.h"
#include "../components/KnobBarComponent.h"
#include "../components/OptionsBarComponent.h"
#include "../components/TitleBarComponent.h"

// Marker-interface for vores skærme/views.
// Brug juce::Component som base; Screen giver os evt. hook til lifecycle senere.
struct Screen
{
    virtual ~Screen() = default;
    virtual juce::String screenName() const = 0;
};

// Hjælpe-base: gør det nemt at lave en view som både er Component og Screen.
class ScreenView : public juce::Component, public Screen
{
public:
    // Option and Knob are defined in shared/ControlTypes.h.
    // These aliases preserve backward compatibility for existing code
    // that references ScreenView::Option and ScreenView::Knob.
    using Option = ::Option;
    using Knob = ::Knob;

    ScreenView();
    ~ScreenView() override = default;

    // Valgfrit: fokuspolitik pr. view
    bool keyPressed (const juce::KeyPress&) override { return false; }
    void paint(juce::Graphics& g) override;

    void resized() override;

    const std::vector<Option>& getOptions() const noexcept { return options; }
    void setOptions(std::vector<Option> newOptions);
    void triggerOption(size_t index);
    void setOptionEnabled(size_t index, bool enabled);
    void setOptionActive(size_t index, bool active);
    void setOptionLabel(size_t index, const juce::String& label);
    void setOptionUpdateCallback(std::function<void()> callback);
    int getOptionIndexForSlot(size_t slotIndex) const; // Get option index for a given slot position (0-3)

    // Pad LED state (managed by screen routing framework)
    const std::array<uint8_t, 16>& getPadLedState() const noexcept { return padLedState_; }
    void setPadLedUpdateCallback(std::function<void()> callback);

    const std::vector<Knob>& getKnobs() const noexcept { return knobs; }
    void setKnobs(std::vector<Knob> newKnobs);
    void handleKnobDelta(size_t index, int delta, bool shift);
    void handleKnobAbsolute(size_t index, uint16_t absoluteValue, uint16_t maxAbsolute = 999);
    void setKnobEnabled(size_t index, bool enabled);
    void setKnobActive(size_t index, bool active);
    virtual void setKnobInputActive(size_t index, bool active);
    void setKnobLabel(size_t index, const juce::String& label);
    void setKnobNumericValue(size_t index, double value, bool notify = false);
    void setKnobListSelection(size_t index, int selectionIndex, bool notify = false);
    void setKnobUpdateCallback(std::function<void()> callback);

    juce::Rectangle<int> getContentBounds() const noexcept { return contentBounds; }

    TitleBarComponent& getTitleBar() noexcept { return titleBar; }
    const TitleBarComponent& getTitleBar() const noexcept { return titleBar; }

    KnobBarComponent& getKnobBar() noexcept { return knobBar; }
    const KnobBarComponent& getKnobBar() const noexcept { return knobBar; }

protected:
    void notifyOptionsUpdated();
    void notifyKnobsUpdated();

    // Views call these to declare their desired pad LED state
    void updatePadLeds(const std::array<uint8_t, 16>& colors);
    void updatePadLed(int index, uint8_t color);

private:
    void updateLayout();
    bool shouldShowOptionBar() const;
    juce::Rectangle<int> knobBarArea() const;
    void refreshOptionDisplay();
    void refreshKnobDisplay();
    juce::String formatKnobValue(const Knob& knob) const;
    bool shouldShowKnobBar() const;
    bool knobLabelsAreEmpty() const;

    OptionsBarComponent optionBar;
    KnobBarComponent knobBar;
    TitleBarComponent titleBar;
    std::vector<Option> options;
    std::vector<Knob> knobs;
    std::vector<double> knobLastAbsoluteValues;
    std::vector<double> knobAbsoluteBaselineNormalized;
    std::vector<double> knobAbsoluteStartNormalized;
    juce::Rectangle<int> contentBounds;
    std::function<void()> optionsChangedCallback;
    std::function<void()> knobsChangedCallback;
    std::array<uint8_t, 16> padLedState_ {};
    std::function<void()> padLedCallback_;
};
