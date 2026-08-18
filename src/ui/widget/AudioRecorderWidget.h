#pragma once

#include <array>
#include <functional>
#include <memory>

#include "Widget.h"
#include "../../engine/PipeWireAudioRecorder.h"
#include "../../engine/SamplePreviewPlayer.h"

/** Dual-panel PipeWire recording suite opened by Shift + Sampling.

    The left panel chooses an input and mono/stereo capture. The right panel
    displays live levels/history and owns Record, Use, and Discard. A used
    take is handed to UiHost, which loads it into the selected pad and opens
    the regular sampling editor. */
class AudioRecorderWidget : public Widget
{
public:
    explicit AudioRecorderWidget(
        std::unique_ptr<AudioInputRecorder> recorder = createPipeWireAudioRecorder(),
        std::unique_ptr<ISamplePreview> previewPlayer = {});
    ~AudioRecorderWidget() override;

    WidgetDescriptor describe() const override;
    juce::String getTitle() const override;
    juce::String getTitleSubtitle() const override;
    juce::Colour getAccentColour() const override;

    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;
    bool acceptsKnobInput(int page, int localIndex) const override;
    void paintPage(juce::Graphics& g, int page,
                   juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute,
                    bool shift) override;
    void handleButton(const controller_events::ButtonEvent& event) override;
    void onUiHostTick() override;

    void setDismissCallback(std::function<void()> callback);
    void setUseTakeCallback(std::function<void(const juce::File&)> callback);

    int getSelectedSourceIndex() const noexcept { return selectedSourceIndex_; }
    int getCaptureChannels() const noexcept { return captureChannels_; }
    bool hasTake() const noexcept { return takeFile_.existsAsFile(); }
    bool isPreviewPlaying() const noexcept;
    juce::File getTakeFile() const { return takeFile_; }
    void toggleRecording();
    void discardTake();
    void useTake();

private:
    void refreshSources();
    void startRecording();
    void stopRecording();
    void toggleMonitoring();
    void restartMonitoring();
    void togglePreview();
    void stopPreview();
    juce::File createTakeFile() const;
    void selectSource(int index);
    void persistSelection();
    void paintInputPanel(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintRecordingPanel(juce::Graphics& g, juce::Rectangle<int> bounds);
    static juce::String formatDuration(double seconds);

    std::unique_ptr<AudioInputRecorder> recorder_;
    std::unique_ptr<ISamplePreview> previewPlayer_;
    int selectedSourceIndex_ { -1 };
    int captureChannels_ { 2 };
    int sourceKnobAccumulator_ { 0 };
    static constexpr int kSourceKnobThreshold = 12;
    juce::File takeFile_;
    bool active_ { false };
    bool wasRecording_ { false };
    bool monitoringRequested_ { false };
    int repaintDivider_ { 0 };
    std::array<float, 144> peakHistory_ {};

    std::function<void()> onDismiss_;
    std::function<void(const juce::File&)> onUseTake_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorderWidget)
};
