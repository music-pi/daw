#pragma once

#include <cstddef>

#include "Widget.h"

class AudioEditorWidget;

/** Right-panel companion for Manual slicing. Start/End remain on the left
    editor; this view visualises the selected range and owns D8 Overlapping. */
class SliceDetailsWidget : public Widget
{
public:
    explicit SliceDetailsWidget(AudioEditorWidget& editor);
    ~SliceDetailsWidget() override = default;

    WidgetDescriptor describe() const override;
    juce::String getTitle() const override { return "Slice Details"; }
    juce::String getTitleSubtitle() const override;
    juce::Colour getAccentColour() const override;

    void onActivated(int panelOffset) override;
    void onDeactivated() override;

    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    void paintPage(juce::Graphics& g, int page,
                   juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;

private:
    void onUiHostTick() override;
    std::size_t stateSignature() const;

    juce::Component::SafePointer<AudioEditorWidget> editor_;
    std::size_t lastSignature_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliceDetailsWidget)
};
