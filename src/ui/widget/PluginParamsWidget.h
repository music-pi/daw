#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include "Widget.h"

namespace te = tracktion::engine;

/** Exposes up to kParamsPerPage of a plugin's automatable parameters on the
    hardware encoders (k1..k4 or k5..k8 depending on which panel the widget
    occupies). Paginated when the plugin has more than kParamsPerPage params.

    Testing hooks: setPlugin / setPage / onEncoderDelta / getVisibleParamCount
    drive the widget directly without hardware. */
class PluginParamsWidget : public Widget
{
public:
    static constexpr int kParamsPerPage = 4;

    PluginParamsWidget() = default;
    ~PluginParamsWidget() override = default;

    // Widget identity
    WidgetDescriptor describe() const override
    {
        return { "plugin_params", 1, false, DisplayConstraint::Any };
    }
    juce::String getTitle() const override { return titleForPlugin_(); }

    // Lifecycle
    void onActivated(int panelOffset) override { panelOffset_ = panelOffset; }
    void onDeactivated() override {}
    void onEditAboutToBeReplaced() override { plugin_ = nullptr; }
    void onEditReplaced() override;

    // Resources
    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int /*page*/) override { return {}; }
    std::vector<Knob> getKnobs(int page) override;

    // Rendering
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;

    // Hardware input — localIndex is 0..3 per Widget::handleKnob contract.
    void handleKnob(int localIndex, int16_t delta, uint16_t /*absolute*/, bool /*shift*/) override
    {
        onEncoderDelta(localIndex, static_cast<float>(delta) / 127.0f);
    }

    // API / testing hooks
    void setPlugin(te::Plugin* plugin);
    te::Plugin* getPlugin() const { return plugin_; }

    int getNumPages() const;
    int getPage() const { return page_; }
    void setPage(int page);

    int getVisibleParamCount() const;

    /** Apply a normalised delta (-1..+1) to the param mapped to localIndex. */
    void onEncoderDelta(int localIndex, float normalisedDelta);

private:
    juce::String titleForPlugin_() const;
    juce::ReferenceCountedArray<te::AutomatableParameter> getCurrentPageParams_() const;

    te::Plugin* plugin_ { nullptr };
    te::EditItemID pluginId_;
    int page_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginParamsWidget)
};
