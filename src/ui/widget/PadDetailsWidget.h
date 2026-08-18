#pragma once

#include <functional>
#include <array>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../control/HardwareConstants.h"
#include "../../engine/SamplerInstrument.h"
#include "../../input/ScopedInputHandler.h"
#include "../components/TextInputComponent.h"
#include "Widget.h"

/**
 * PadDetailsWidget -- displays details for the currently selected pad.
 *
 * Shows pad name, sample info, MIDI mapping, and provides choke group editing.
 * Sample browsing is delegated to UiHost via a callback.
 */
class PadDetailsWidget : public Widget
{
public:
    PadDetailsWidget();
    ~PadDetailsWidget() override;

    // -- Widget identity --
    WidgetDescriptor describe() const override;
    juce::String getTitle() const override;
    juce::Colour getAccentColour() const override;

    // -- Lifecycle --
    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    void onPageVisible(int page) override;

    // -- Resources --
    std::vector<std::string> requiredResources(int page) override;

    // -- Options & Knobs --
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    // -- Rendering --
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;

    // -- Input --
    void handlePad(const controller_events::PadEvent& e) override;
    void handleButton(const controller_events::ButtonEvent& e) override;
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;
    void refreshPadLeds() override;

    /** Set a callback invoked when the user presses escape/back to dismiss. */
    void setBackCallback(std::function<void()> callback);

    /** Set a callback to request sample browser for the current pad. */
    void setBrowseCallback(std::function<void(int padId)> callback);
    void setBrowseLayerCallback(std::function<void(int padId)> callback);

    /** UiHost installs this callback so the widget can open the plugin
        browser (Instruments mode) against the current pad. Fires with the
        currently selected pad index when the user invokes "Set Instrument". */
    void setOnSetInstrumentRequested(std::function<void(int padIndex)> cb)
    {
        onSetInstrument_ = std::move(cb);
    }

    /** UiHost installs this callback to dispatch ClearPadInstrumentCommand
        when the user invokes "Clear Instrument". Fires with the currently
        selected pad index. */
    void setOnClearInstrumentRequested(std::function<void(int padIndex)> cb)
    {
        onClearInstrument_ = std::move(cb);
    }

    /** Test hooks — drive the same dispatch paths as the option buttons so
        headless tests don't need a hardware controller. */
    void invokeSetInstrumentForTesting()   { triggerSetInstrument(); }
    void invokeClearInstrumentForTesting() { triggerClearInstrument(); }

    /** Get the currently selected pad ID, or -1 if none. */
    int getCurrentPadId() const { return currentPadId_; }

private:
    static constexpr size_t kStartKnobIndex = 0;
    static constexpr size_t kEndKnobIndex = 1;
    static constexpr size_t kGainKnobIndex = 2;
    static constexpr size_t kChokeKnobIndex = 3;

    void onUiHostTick() override;
    void refreshSelection();
    std::size_t computeVisualSignature() const;
    void triggerSetInstrument();
    void triggerClearInstrument();
    void onChokeGroupChanged(int selectionIndex);
    void cycleTriggerMode();
    void onGainChanged(double db);
    void onRangeChanged(double startSeconds, double endSeconds);
    void setLayerGain(double db);
    void setLayerWeight(double percent);
    void setLayerVelocityCurve(int index);
    void setLayerVelocityMinimum(double percent);
    void setLayerVelocityMaximum(double percent);
    void updatePageArrowLeds();
    void setLayerPadMode(bool enabled);
    void handleLayerPadInput(InputEvent& event);
    void restoreLayerPadOwners();
    void updateThumbnail();
    int chokeStorageFromIndex(int selectionIndex) const;
    int chokeIndexFromStorage(int storage) const;

    /** Stash the opposite-panel widget, seed renameField_ with the current
        pad's name, and open the T9 flow. Restored on commit / cancel. */
    void beginPadRename();
    void restoreRenameStashedWidget();

    // Tick divider: onUiHostTick fires at 60 Hz, original poll was 10 Hz → N=6
    int tickDiv_ { 0 };

    static constexpr int kKnobThreshold = 12;  // raw ticks per discrete step
    std::array<int, 4> knobAccumulator_ { 0, 0, 0, 0 };

    std::vector<Knob> currentKnobs_;
    SamplerInstrument::PadSnapshot currentPad_;
    bool hasSelection_ { false };
    int currentPadId_ { -1 };
    int selectedLayerIndex_ { 0 };
    bool layerPadMode_ { false };
    std::array<bool, SamplerInstrument::kMaxSampleLayers> layerPadsHeld_ {};
    std::array<std::string, HardwareConstants::kPadCount> savedPadOwners_ {};
    ScopedInputHandler layerPadBinding_;

    juce::String padTitle_;
    juce::String sampleStatus_;
    juce::String midiInfo_;
    juce::String samplePath_;

    juce::AudioFormatManager thumbnailFormatManager_;
    juce::AudioThumbnailCache thumbnailCache_ { 1 };
    std::unique_ptr<juce::AudioThumbnail> thumbnail_;
    juce::String loadedThumbnailPath_;
    juce::Colour cachedAccent_ { UiTheme::kTitlebarAccent };
    float sampleGainDb_ { 0.0f };

    std::function<void()> backCallback_;
    std::function<void(int padId)> browseCallback_;
    std::function<void(int padId)> browseLayerCallback_;
    std::function<void(int padIndex)> onSetInstrument_;
    std::function<void(int padIndex)> onClearInstrument_;
    juce::String instrumentName_;

    // T9 driver for the pad-rename flow. The component never renders itself
    // (addChildComponent, not addAndMakeVisible) — it just owns the state
    // machine and spawns a T9Widget on the opposite panel while editing.
    TextInputComponent renameField_;
    // Opposite-panel widget stashed while the T9 is up, popped back when the
    // rename commits or cancels. Mirrors PatternWidget's rename flow; uses
    // the opposite side because PadDetailsWidget can mount on either panel.
    std::unique_ptr<Widget> renameStashedRight_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadDetailsWidget)
};
