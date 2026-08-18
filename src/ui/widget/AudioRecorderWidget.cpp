#include "AudioRecorderWidget.h"

#include "../../app/DataPaths.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../theme/UiTheme.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto kInputNodeProperty = "recordingInputNode";
constexpr auto kInputChannelsProperty = "recordingInputChannels";
}

AudioRecorderWidget::AudioRecorderWidget(
    std::unique_ptr<AudioInputRecorder> recorder,
    std::unique_ptr<ISamplePreview> previewPlayer)
    : recorder_(std::move(recorder))
    , previewPlayer_(std::move(previewPlayer))
{
    jassert(recorder_ != nullptr);
}

AudioRecorderWidget::~AudioRecorderWidget()
{
    if (recorder_ != nullptr)
        recorder_->stopRecording();
}

WidgetDescriptor AudioRecorderWidget::describe() const
{
    return { "audio_recorder", 2, false, DisplayConstraint::Both };
}

juce::String AudioRecorderWidget::getTitle() const
{
    return "Audio Recording";
}

juce::String AudioRecorderWidget::getTitleSubtitle() const
{
    if (recorder_ != nullptr && recorder_->isRecording())
        return "REC " + formatDuration(recorder_->getDurationSeconds());
    if (isPreviewPlaying())
        return "Previewing take";
    if (hasTake())
        return "Take ready";
    return "Shift + Sampling";
}

juce::Colour AudioRecorderWidget::getAccentColour() const
{
    return juce::Colour(0xffe34b4b);
}

void AudioRecorderWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    active_ = true;
    engine().setAudioCaptureState(true, false);

    const auto settings = engine().getSettingsState();
    captureChannels_ = juce::jlimit(
        1, 2, static_cast<int>(settings.getProperty(kInputChannelsProperty, 2)));
    const auto storedNode = settings.getProperty(kInputNodeProperty).toString();
    refreshSources();

    const auto& sources = recorder_->getSources();
    for (int i = 0; i < static_cast<int>(sources.size()); ++i)
    {
        if (sources[static_cast<size_t>(i)].nodeName == storedNode)
        {
            selectedSourceIndex_ = i;
            break;
        }
    }
    if (selectedSourceIndex_ < 0 && !sources.empty())
        selectedSourceIndex_ = 0;
    selectSource(selectedSourceIndex_);
}

void AudioRecorderWidget::onDeactivated()
{
    active_ = false;
    if (recorder_ != nullptr)
    {
        recorder_->stopRecording();
        recorder_->stopMonitoring();
    }
    stopPreview();
    monitoringRequested_ = false;
    engine().setAudioCaptureState(false, false);
}

std::vector<std::string> AudioRecorderWidget::requiredResources(int page)
{
    std::vector<std::string> resources;
    const int first = page == 0 ? 1 : 5;
    for (int i = first; i < first + 4; ++i)
    {
        resources.push_back("d" + std::to_string(i));
        resources.push_back("k" + std::to_string(i));
    }
    if (page == 0)
        resources.push_back("sampling");
    return resources;
}

std::vector<Option> AudioRecorderWidget::getOptions(int page)
{
    if (page == 0)
    {
        const bool recording = recorder_->isRecording();
        return {
            { "record.refresh", "Refresh",
              recording ? OptionState::Disabled : OptionState::Enabled,
              1, false, [this]()
              {
                  if (!recorder_->isRecording())
                      refreshSources();
              } },
            { "record.channels", captureChannels_ == 1 ? "Mono" : "Stereo",
              OptionState::Active, 1, false,
              [this]()
              {
                  if (recorder_->isRecording())
                      return;
                  captureChannels_ = captureChannels_ == 1 ? 2 : 1;
                  selectSource(selectedSourceIndex_);
                  persistSelection();
                  repaint();
              } },
            { "", "", OptionState::Empty },
            { "record.close", "Close", OptionState::Enabled, 1, false,
              [this]()
              {
                  if (onDismiss_ != nullptr)
                      onDismiss_();
              } }
        };
    }

    const bool recording = recorder_->isRecording();
    const bool takeReady = !recording && hasTake();
    const bool previewing = isPreviewPlaying();
    const bool monitoring = recorder_->isMonitoring();
    const bool canMonitor = selectedSourceIndex_ >= 0;
    const bool canStart = selectedSourceIndex_ >= 0 && !takeReady;
    const auto primaryLabel = (recording || previewing) ? juce::String("Stop")
        : (takeReady ? juce::String("Preview") : juce::String("Record"));
    const auto primaryState = (recording || previewing) ? OptionState::Active
        : ((takeReady || canStart) ? OptionState::Enabled : OptionState::Disabled);
    return {
        { "record.monitor", "Monitor",
          monitoring ? OptionState::Active
                     : (canMonitor ? OptionState::Enabled : OptionState::Disabled),
          1, false, [this]() { toggleMonitoring(); } },
        { "record.toggle", primaryLabel, primaryState,
          1, false, [this]()
          {
              if (recorder_->isRecording() || !hasTake())
                  toggleRecording();
              else
                  togglePreview();
          } },
        { "record.use", "Use", takeReady ? OptionState::Enabled
                                           : OptionState::Disabled,
          1, false, [this]() { useTake(); } },
        { "record.discard", "Discard", takeReady ? OptionState::Enabled
                                                   : OptionState::Disabled,
          1, false, [this]() { discardTake(); } }
    };
}

std::vector<Knob> AudioRecorderWidget::getKnobs(int page)
{
    if (page != 0)
        return {};

    Knob source;
    source.id = "record.input";
    source.label = "PipeWire Input";
    source.isEnabled = !recorder_->isRecording() && !recorder_->getSources().empty();
    source.continuousMode = true;
    Knob::ListModel model;
    for (const auto& item : recorder_->getSources())
        model.entries.push_back(item.displayName);
    model.selectedIndex = juce::jmax(0, selectedSourceIndex_);
    model.onChange = [this](int index) { selectSource(index); };
    source.model = std::move(model);
    source.onAdjust = [this](int delta, bool)
    {
        if (recorder_->isRecording())
            return;
        sourceKnobAccumulator_ += delta;
        if (std::abs(sourceKnobAccumulator_) < kSourceKnobThreshold)
            return;
        const int direction = sourceKnobAccumulator_ > 0 ? 1 : -1;
        sourceKnobAccumulator_ = 0;
        selectSource(selectedSourceIndex_ + direction);
    };

    return { source };
}

bool AudioRecorderWidget::acceptsKnobInput(int page, int localIndex) const
{
    return page == 0 && localIndex == 0;
}

void AudioRecorderWidget::paintPage(juce::Graphics& g, int page,
                                    juce::Rectangle<int> bounds)
{
    g.fillAll(UiTheme::kBackgroundDark);
    if (page == 0)
        paintInputPanel(g, bounds);
    else
        paintRecordingPanel(g, bounds);
}

void AudioRecorderWidget::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.getWidth() >= UiTheme::kTotalWidth)
    {
        auto left = bounds.removeFromLeft(UiTheme::kPanelWidth);
        auto right = bounds.removeFromLeft(UiTheme::kPanelWidth);
        left.removeFromTop(UiTheme::kOptionHeight);
        right.removeFromTop(UiTheme::kOptionHeight);
        left.removeFromBottom(UiTheme::kKnobBarWithTitleHeight);
        paintPage(g, 0, left);
        paintPage(g, 1, right);
        return;
    }
    paintPage(g, currentPage_, bounds);
}

void AudioRecorderWidget::handleKnob(int localIndex, int16_t delta,
                                     uint16_t, bool shift)
{
    if (localIndex != 0)
        return;
    auto knobs = getKnobs(0);
    if (!knobs.empty() && knobs[0].onAdjust)
        knobs[0].onAdjust(delta, shift);
}

void AudioRecorderWidget::handleButton(
    const controller_events::ButtonEvent& event)
{
    if (!event.pressed)
        return;
    if (event.name == "recCountIn")
        toggleRecording();
    else if (event.name == "stop")
    {
        if (recorder_->isRecording())
            stopRecording();
        else
            stopPreview();
    }
}

void AudioRecorderWidget::onUiHostTick()
{
    const bool recording = recorder_->isRecording();
    engine().setAudioCaptureState(active_, recording);

    if (recording)
    {
        std::move(peakHistory_.begin() + 1, peakHistory_.end(),
                  peakHistory_.begin());
        peakHistory_.back() = juce::jmax(recorder_->getPeak(0),
                                         recorder_->getPeak(1));
    }
    else if (wasRecording_)
    {
        stopRecording();
        const auto error = recorder_->getLastError();
        if (error.isNotEmpty())
            showToast(ToastKind::Error, error);
    }
    wasRecording_ = recording;

    if (recording || (++repaintDivider_ % 30) == 0)
        repaint();
}

void AudioRecorderWidget::setDismissCallback(std::function<void()> callback)
{
    onDismiss_ = std::move(callback);
}

void AudioRecorderWidget::setUseTakeCallback(
    std::function<void(const juce::File&)> callback)
{
    onUseTake_ = std::move(callback);
}

void AudioRecorderWidget::toggleRecording()
{
    if (recorder_->isRecording())
        stopRecording();
    else if (hasTake())
        showToast(ToastKind::Info, "Use or discard the current take");
    else
        startRecording();
}

void AudioRecorderWidget::discardTake()
{
    stopPreview();
    if (recorder_->isRecording())
        stopRecording();
    if (takeFile_.existsAsFile())
        takeFile_.deleteFile();
    takeFile_ = juce::File();
    peakHistory_.fill(0.0f);
    repaint();
}

void AudioRecorderWidget::useTake()
{
    if (!hasTake() || onUseTake_ == nullptr)
        return;
    stopPreview();
    onUseTake_(takeFile_);
}

bool AudioRecorderWidget::isPreviewPlaying() const noexcept
{
    return previewPlayer_ != nullptr && previewPlayer_->isPlaying();
}

void AudioRecorderWidget::refreshSources()
{
    const auto previousNode = juce::isPositiveAndBelow(
        selectedSourceIndex_, static_cast<int>(recorder_->getSources().size()))
        ? recorder_->getSources()[static_cast<size_t>(selectedSourceIndex_)].nodeName
        : juce::String();

    if (!recorder_->refreshSources())
    {
        selectedSourceIndex_ = -1;
        showToast(ToastKind::Error, recorder_->getLastError());
        repaint();
        return;
    }

    selectedSourceIndex_ = 0;
    const auto& sources = recorder_->getSources();
    for (int i = 0; i < static_cast<int>(sources.size()); ++i)
        if (sources[static_cast<size_t>(i)].nodeName == previousNode)
            selectedSourceIndex_ = i;
    selectSource(selectedSourceIndex_);
}

void AudioRecorderWidget::startRecording()
{
    stopPreview();
    const auto& sources = recorder_->getSources();
    if (!juce::isPositiveAndBelow(selectedSourceIndex_,
                                  static_cast<int>(sources.size())))
    {
        showToast(ToastKind::Error, "No PipeWire input selected");
        return;
    }

    takeFile_ = createTakeFile();
    if (!recorder_->startRecording(
            sources[static_cast<size_t>(selectedSourceIndex_)],
            captureChannels_, takeFile_))
    {
        takeFile_ = juce::File();
        showToast(ToastKind::Error, recorder_->getLastError());
        return;
    }

    wasRecording_ = true;
    peakHistory_.fill(0.0f);
    engine().setAudioCaptureState(true, true);
    repaint();
}

void AudioRecorderWidget::stopRecording()
{
    recorder_->stopRecording();
    wasRecording_ = false;
    engine().setAudioCaptureState(active_, false);

    if (takeFile_.existsAsFile() && takeFile_.getSize() <= 44)
    {
        takeFile_.deleteFile();
        takeFile_ = juce::File();
        showToast(ToastKind::Error, "No audio was captured");
    }
    repaint();
}

void AudioRecorderWidget::toggleMonitoring()
{
    if (recorder_->isMonitoring())
    {
        monitoringRequested_ = false;
        recorder_->stopMonitoring();
        repaint();
        return;
    }

    stopPreview();
    monitoringRequested_ = true;
    restartMonitoring();
    if (recorder_->isMonitoring())
        showToast(ToastKind::Warning, "Monitor on - watch for feedback");
}

void AudioRecorderWidget::restartMonitoring()
{
    recorder_->stopMonitoring();
    if (!monitoringRequested_)
        return;

    const auto& sources = recorder_->getSources();
    if (!juce::isPositiveAndBelow(selectedSourceIndex_,
                                  static_cast<int>(sources.size()))
        || !recorder_->startMonitoring(
            sources[static_cast<size_t>(selectedSourceIndex_)],
            captureChannels_))
    {
        monitoringRequested_ = false;
        showToast(ToastKind::Error, recorder_->getLastError());
    }
    repaint();
}

void AudioRecorderWidget::togglePreview()
{
    if (isPreviewPlaying())
    {
        stopPreview();
        return;
    }
    if (!hasTake())
        return;

    // A take preview should be heard in isolation, not mixed with the live
    // source. Monitoring can be explicitly re-enabled afterwards.
    monitoringRequested_ = false;
    recorder_->stopMonitoring();
    if (previewPlayer_ == nullptr)
        previewPlayer_ = std::make_unique<SamplePreviewPlayer>(engine());
    previewPlayer_->play(takeFile_);
    repaint();
}

void AudioRecorderWidget::stopPreview()
{
    if (previewPlayer_ != nullptr)
        previewPlayer_->stop();
    repaint();
}

juce::File AudioRecorderWidget::createTakeFile() const
{
    auto directory = DataPaths::getSamplesDir().getChildFile("recordings");
    directory.createDirectory();
    const auto timestamp = juce::Time::getCurrentTime().formatted(
        "%Y%m%d-%H%M%S") + "-"
        + juce::String(juce::Time::getMillisecondCounter() % 1000)
              .paddedLeft('0', 3);
    const auto uniqueSuffix = juce::Uuid().toString();
    return directory.getChildFile(
        "take-" + timestamp + "-" + uniqueSuffix + ".wav");
}

void AudioRecorderWidget::selectSource(int index)
{
    const auto& sources = recorder_->getSources();
    if (sources.empty())
    {
        selectedSourceIndex_ = -1;
        return;
    }
    selectedSourceIndex_ = juce::jlimit(
        0, static_cast<int>(sources.size()) - 1, index);
    captureChannels_ = juce::jmin(
        captureChannels_,
        sources[static_cast<size_t>(selectedSourceIndex_)].channels);
    persistSelection();
    restartMonitoring();
    repaint();
}

void AudioRecorderWidget::persistSelection()
{
    if (!hasAudioEngine())
        return;
    auto settings = engine().getSettingsState();
    settings.setProperty(kInputChannelsProperty, captureChannels_, nullptr);
    const auto& sources = recorder_->getSources();
    if (juce::isPositiveAndBelow(selectedSourceIndex_,
                                 static_cast<int>(sources.size())))
        settings.setProperty(kInputNodeProperty,
                             sources[static_cast<size_t>(selectedSourceIndex_)].nodeName,
                             nullptr);
}

void AudioRecorderWidget::paintInputPanel(juce::Graphics& g,
                                          juce::Rectangle<int> bounds)
{
    auto content = bounds.reduced(18, 14);
    g.setColour(UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));
    g.drawText("PIPEWIRE INPUT", content.removeFromTop(18),
               juce::Justification::centredLeft);

    const auto& sources = recorder_->getSources();
    const bool hasSource = juce::isPositiveAndBelow(
        selectedSourceIndex_, static_cast<int>(sources.size()));
    g.setColour(hasSource ? juce::Colours::white : UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody,
                                          juce::Font::bold)));
    g.drawFittedText(hasSource
                         ? sources[static_cast<size_t>(selectedSourceIndex_)].displayName
                         : recorder_->getLastError(),
                     content.removeFromTop(52), juce::Justification::centredLeft,
                     2);

    content.removeFromTop(8);
    g.setColour(UiTheme::kOptionFillEmpty);
    auto detail = content.removeFromTop(52);
    g.fillRect(detail);
    g.setColour(UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kOptionLabel)));
    g.drawText("Capture", detail.removeFromTop(22).reduced(10, 0),
               juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.drawText(captureChannels_ == 1 ? "Mono / 48 kHz / 24-bit"
                                    : "Stereo / 48 kHz / 24-bit",
               detail.reduced(10, 0), juce::Justification::centredLeft);

    g.setColour(UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));
    g.drawFittedText("Turn K1 to choose any PipeWire source. Recording only "
                     "activates while REC is running.",
                     content.removeFromBottom(38),
                     juce::Justification::bottomLeft, 2);
}

void AudioRecorderWidget::paintRecordingPanel(
    juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto content = bounds.reduced(18, 14);
    const bool recording = recorder_->isRecording();

    g.setColour(recording ? juce::Colour(0xffe34b4b) : UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody,
                                          juce::Font::bold)));
    const auto stateLabel = recording ? juce::String("RECORDING")
        : (isPreviewPlaying() ? juce::String("PREVIEWING")
        : (hasTake() ? juce::String("TAKE READY")
                     : (recorder_->isMonitoring() ? juce::String("MONITORING")
                                                  : juce::String("READY"))));
    g.drawText(stateLabel,
               content.removeFromTop(24), juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.drawText(formatDuration(recorder_->getDurationSeconds()),
               content.withHeight(24), juce::Justification::centredRight);
    content.removeFromTop(8);

    auto waveform = content.removeFromTop(105);
    g.setColour(UiTheme::kOptionFillEmpty);
    g.fillRect(waveform);
    g.setColour(UiTheme::kOptionBorderEmpty);
    g.drawRect(waveform);
    const float centreY = static_cast<float>(waveform.getCentreY());
    const float dx = static_cast<float>(waveform.getWidth())
        / static_cast<float>(peakHistory_.size());
    g.setColour(getAccentColour());
    for (size_t i = 0; i < peakHistory_.size(); ++i)
    {
        const float height = peakHistory_[i]
            * static_cast<float>(waveform.getHeight() - 8) * 0.5f;
        const float x = static_cast<float>(waveform.getX())
            + static_cast<float>(i) * dx;
        g.drawVerticalLine(static_cast<int>(x), centreY - height,
                           centreY + height);
    }

    content.removeFromTop(10);
    const float leftPeak = recorder_->getPeak(0);
    const float rightPeak = recorder_->getPeak(1);
    auto drawMeter = [&g](juce::Rectangle<int> area, float peak,
                          const juce::String& label)
    {
        g.setColour(UiTheme::kOptionFillEmpty);
        g.fillRect(area);
        g.setColour(peak >= 0.99f ? juce::Colours::red
                                 : juce::Colour(0xff58c879));
        g.fillRect(area.withWidth(static_cast<int>(area.getWidth() * peak)));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel,
                                              juce::Font::bold)));
        g.drawText(label, area.reduced(5, 0), juce::Justification::centredLeft);
    };
    drawMeter(content.removeFromTop(14), leftPeak, "L");
    content.removeFromTop(4);
    drawMeter(content.removeFromTop(14), rightPeak, "R");
}

juce::String AudioRecorderWidget::formatDuration(double seconds)
{
    const int totalTenths = juce::jmax(0, static_cast<int>(seconds * 10.0));
    const int minutes = totalTenths / 600;
    const int wholeSeconds = (totalTenths / 10) % 60;
    const int tenths = totalTenths % 10;
    return juce::String::formatted("%02d:%02d.%d", minutes, wholeSeconds, tenths);
}
