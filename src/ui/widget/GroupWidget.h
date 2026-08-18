#pragma once

#include <functional>

#include "Widget.h"

/**
 * GroupWidget — system-level widget that owns group LEDs (g1-g8).
 *
 * Always active, no visual panel (pageCount=0), LED-only.
 * Handles group button presses for group recall.
 * Updates LED colors to reflect active/data/empty group state.
 */
class GroupWidget : public Widget
{
public:
    GroupWidget() = default;
    ~GroupWidget() override = default;

    // ── Widget interface ─────────────────────────────────────────────────

    WidgetDescriptor describe() const override;
    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    std::vector<std::string> requiredResources(int page) override;

    std::vector<Option> getOptions(int /*page*/) override { return {}; }
    std::vector<Knob> getKnobs(int /*page*/) override { return {}; }
    void paintPage(juce::Graphics&, int, juce::Rectangle<int>) override {}
    void paint(juce::Graphics&) override {}

    // ── Input ────────────────────────────────────────────────────────────

    void handleButton(const controller_events::ButtonEvent& e) override;

    // ── LED update (callable externally for refresh) ─────────────────────

    void updateLeds();

    // ── Callbacks ────────────────────────────────────────────────────────

    /** Set callback invoked when shift+group button opens group details. */
    void setGroupDetailsCallback(std::function<void(int)> callback)
    {
        groupDetailsCallback_ = std::move(callback);
    }

private:
    std::function<void(int)> groupDetailsCallback_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupWidget)
};
