#pragma once

#include <functional>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widget.h"

/**
 * GroupDetailsDialog -- modal dialog for viewing/editing group details.
 *
 * New-architecture equivalent of GroupDetailsView. Shows group info (color,
 * status, sample count) and provides create/delete/close options.
 *
 * Opened as a dialog via WindowManager::showDialog() with forceOnTop=true.
 * Delete confirmation spawns a nested dialog (second level of dialog stack).
 */
class GroupDetailsDialog : public Widget
{
public:
    GroupDetailsDialog();
    ~GroupDetailsDialog() override = default;

    // -- Widget identity --
    WidgetDescriptor describe() const override;

    // -- Lifecycle --
    void onActivated(int panelOffset) override;
    void onDeactivated() override;

    // -- Resources --
    std::vector<std::string> requiredResources(int page) override;

    // -- Options & Knobs --
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    // -- Rendering --
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;

    // -- Input --
    void handleOption(int localIndex) override;

    // -- Configuration --
    void setGroupIndex(int index);

    /** Set a callback invoked when the dialog should dismiss itself. */
    void setDismissCallback(std::function<void()> callback);

    // -- Test helpers --
    bool isConfirmingDelete() const { return confirmingDelete_; }

private:
    void refreshState();

    std::function<void()> onDismiss_;

    int groupIndex_ { -1 };
    uint8_t groupColor_ { 0 };
    bool groupHasData_ { false };
    bool groupIsActive_ { false };
    int loadedSampleCount_ { 0 };
    bool confirmingDelete_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupDetailsDialog)
};
