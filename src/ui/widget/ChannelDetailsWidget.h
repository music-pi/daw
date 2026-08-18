#pragma once

#include <functional>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include "Widget.h"
#include "../components/mixer/ChannelDescriptor.h"

namespace te = tracktion::engine;

/**
 * ChannelDetailsWidget — right-panel companion to MixerWidget.
 *
 * Layout (from the Mixer Details mockup):
 *   • Titlebar:  "Channel > <name>"  +  subtitle sample filename
 *   • Inserts:   full-width plugin-slot list (filled + "Empty" placeholders)
 *   • Sends:     4×3 grid of aux-send slots (labelled when populated)
 *   • Add/Open EQ action: opens the dedicated full-panel EqualiserWidget
 */
class ChannelDetailsWidget : public Widget,
                             private juce::ValueTree::Listener
{
public:
    enum class EqBand { Low = 0, Mid1, Mid2, High };

    ChannelDetailsWidget();
    ~ChannelDetailsWidget() override;

    WidgetDescriptor describe() const override;

    juce::String getTitle() const override;
    juce::String getTitleSubtitle() const override;
    juce::String getKnobBarTitle() const override { return {}; }

    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    void onEditAboutToBeReplaced() override;
    void onEditReplaced() override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;
    void handleOption(int localIndex) override;

    /** UiHost installs this callback so the widget can open PluginBrowserWidget
        (effects mode) against the active channel's PluginList. Fires with the
        resolved target list when the "Add Insert" option is invoked. The
        callback may be null before UiHost has wired it up; in that case the
        option is a no-op. */
    void setOnAddInsertRequested(std::function<void(te::PluginList*)> cb)
    {
        onAddInsertRequested_ = std::move(cb);
    }

    /** Requests either insertion or opening of the native channel EQ. If an
        EQ already exists, equaliser is non-null; otherwise UiHost inserts one
        into targetList before opening the editor. */
    void setOnEqRequested(
        std::function<void(te::PluginList*, te::EqualiserPlugin*)> cb)
    {
        onEqRequested_ = std::move(cb);
    }

    /** Test hook. Drives the same dispatch path as the "Add Insert" option
        button so headless tests don't need a hardware controller. */
    void invokeAddInsertForTesting() { triggerAddInsert(); }
    void invokeEqForTesting() { triggerEq(); }

private:
    struct SendSlot
    {
        juce::String label;         // empty when slot is unused
        float gainDb { 0.0f };
        bool active { false };      // has an AuxSendPlugin assigned
    };

    static constexpr int kInsertRowCount = 5;
    static constexpr int kSendCols = 4;
    static constexpr int kSendRows = 3;
    static constexpr int kSendSlotCount = kSendCols * kSendRows;

    void onUiHostTick() override;

    // ValueTree::Listener — re-resolve the active channel as soon as
    // MixerWidget publishes a new level / focusedGroup / activeChannelId.
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    void resolveActiveChannel();
    ChannelDescriptor findActiveChannel();
    te::EqualiserPlugin* findEq();
    te::PluginList* resolveTargetPluginList();
    void triggerAddInsert();
    void triggerEq();
    void rebuildKnobs();
    void refreshPluginLists();
    juce::String resolveSampleFilename() const;
    std::size_t computeVisualSignature() const;

    void paintInserts(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintSends(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintEqCurve(juce::Graphics& g, juce::Rectangle<int> bounds);

    juce::ValueTree watchedMixerState_;
    bool resumeAfterEditReplacement_ { false };

    ChannelDescriptor currentDescriptor_;
    te::EqualiserPlugin* currentEq_ { nullptr };
    EqBand selectedBand_ { EqBand::Low };

    std::vector<juce::String> insertsList_;
    std::array<SendSlot, kSendSlotCount> sendSlots_ {};
    int pluginCount_ { 0 };
    juce::String sampleFileName_;
    // Cached title: composed in resolveActiveChannel(), read per-frame by
    // WindowManager's titlebar refresh — avoids a String concat every tick.
    juce::String cachedTitle_ { "Channel > —" };

    std::vector<Knob> currentKnobs_;

    // Tick divider: onUiHostTick fires at 60 Hz, original poll was 20 Hz → N=3
    int tickDiv_ { 0 };

    std::function<void(te::PluginList*)> onAddInsertRequested_;
    std::function<void(te::PluginList*, te::EqualiserPlugin*)> onEqRequested_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelDetailsWidget)
};
