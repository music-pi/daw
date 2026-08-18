#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "../../engine/PluginCatalog.h"
#include "../components/ListComponent.h"
#include "../components/Toast.h"
#include "Widget.h"

/** Simple MK3-native plugin picker.
    Renders the PluginCatalog filtered as instruments or effects via a
    shared ListComponent child, lets callers scroll + confirm. 960x272.

    Testing hooks (getItemCount/getSelectedIndex/moveSelection/confirmSelection
    /cancel) let GTest drive it headless - production input comes via nav
    buttons (navUp/navDown/navPush) and the d1/d2/d4 option buttons. */
class PluginBrowserWidget : public Widget
{
public:
    enum class Filter { Instruments, Effects };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void pluginSelected(const juce::PluginDescription&) = 0;
        virtual void browserCancelled() {}
    };

    PluginBrowserWidget(PluginCatalog& catalog, Filter filter);
    ~PluginBrowserWidget() override = default;

    // Widget identity
    WidgetDescriptor describe() const override
    {
        return { "plugin_browser", 1, false, DisplayConstraint::Any };
    }
    juce::String getTitle() const override
    {
        return filter_ == Filter::Instruments ? "Instruments" : "Effects";
    }

    // Lifecycle
    void onActivated(int panelOffset) override;
    void onDeactivated() override;

    // Resources / bars
    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int /*page*/) override { return {}; }

    // Rendering — background + empty-state message; the list paints itself.
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void resized() override;

    // Hardware input
    void handleButton(const controller_events::ButtonEvent& e) override;

    // API / testing hooks
    int getItemCount() const { return static_cast<int>(items_.size()); }
    int getSelectedIndex() const;
    Filter getFilter() const { return filter_; }
    void moveSelection(int delta);
    void confirmSelection();
    void cancel();
    void refreshItems();

    void setListener(Listener* l) { listener_ = l; }

    /** Kick a catalog scan. Safe to call repeatedly. Shows a loading toast
        while in flight; completion callback repopulates the list on the
        message thread. */
    void beginScan();

    bool isScanning() const { return scanning_; }

private:
    PluginCatalog& catalog_;
    Filter filter_;
    std::vector<juce::PluginDescription> items_;
    Listener* listener_ { nullptr };

    ListComponent list_;
    std::optional<ToastHandle> scanToast_;

    bool scanning_ { false };
    int scanProgress_ { 0 };
    int scanTotal_ { 0 };

    void rebuildList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserWidget)
};
