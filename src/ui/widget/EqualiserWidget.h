#pragma once

#include <array>

#include <juce_data_structures/juce_data_structures.h>
#include <tracktion_engine/tracktion_engine.h>

#include "Widget.h"

namespace te = tracktion::engine;

/** Full-panel editor for Tracktion Engine's native four-band equaliser.

    The widget owns no DSP. It edits the automatable parameters on an
    EqualiserPlugin already inserted in a pad, group, or master PluginList.
    D1-D4 select/toggle points; K1-K4 edit frequency, gain, Q, and enabled. */
class EqualiserWidget : public Widget,
                        private juce::ValueTree::Listener
{
public:
    enum class Band { Low = 0, Mid1, Mid2, High };

    EqualiserWidget(te::EqualiserPlugin& equaliser, juce::String channelName);
    ~EqualiserWidget() override;

    WidgetDescriptor describe() const override;
    juce::String getTitle() const override;
    juce::String getKnobBarTitle() const override { return "Selected Point"; }

    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    void onEditAboutToBeReplaced() override;
    void onEditReplaced() override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    void paint(juce::Graphics& g) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void onUiHostTick() override;
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;
    void handleOption(int localIndex) override;

    Band selectedBand() const noexcept { return selectedBand_; }
    bool isBandEnabled(Band band) const;

private:
    static constexpr int kAllBandsMask = 0x0f;

    te::AutomatableParameter* frequencyParameter(Band band) const;
    te::AutomatableParameter* gainParameter(Band band) const;
    te::AutomatableParameter* qParameter(Band band) const;

    int enabledBandsMask() const;
    void setBandEnabled(Band band, bool enabled);
    void invokeBandOption(int bandIndex);
    void setParameter(te::AutomatableParameter* parameter, float value);

    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}

    te::Plugin::Ptr pluginLifetime_;
    te::EqualiserPlugin* equaliser_ { nullptr };
    te::EditItemID pluginId_;
    juce::String channelName_;
    Band selectedBand_ { Band::Low };
    bool listening_ { false };
    bool resumeAfterEditReplacement_ { false };
    bool closePosted_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqualiserWidget)
};
