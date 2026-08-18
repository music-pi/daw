#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widget.h"
#include "WidgetTypes.h"

/**
 * ConfirmDialog — minimal modal dialog asking for a yes/no confirmation.
 *
 * Occupies one panel (Left or Right — defaults to Left, or the focused side
 * when set via setPreferredSide before showDialog). The opposite panel is
 * dimmed by WindowManager while the dialog is open.
 *
 * Option-bar slot d1 = Cancel and d4 = Confirm (labelled with the provided
 * confirmLabel, e.g. "Clear", "Delete"). The dialog dismisses itself after
 * either button is pressed.
 */
class ConfirmDialog : public Widget
{
public:
    ConfirmDialog(juce::String title,
                  juce::String message,
                  juce::String confirmLabel,
                  std::function<void()> onConfirm,
                  juce::Colour confirmColour = juce::Colours::orangered);
    ~ConfirmDialog() override = default;

    /** Pin the dialog to a specific panel (default Left). Must be set before
        showDialog; describe() reads it. */
    void setPreferredSide(DisplaySide side) { preferredSide_ = side; }
    DisplaySide getPreferredSide() const { return preferredSide_; }

    WidgetDescriptor describe() const override;

    void onActivated(int panelOffset) override;
    void onDeactivated() override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int /*page*/) override { return {}; }

    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;

private:
    void dismiss();

    juce::String title_;
    juce::String message_;
    juce::String confirmLabel_;
    std::function<void()> onConfirm_;
    juce::Colour confirmColour_;
    DisplaySide preferredSide_ { DisplaySide::Left };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfirmDialog)
};
