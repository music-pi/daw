#include "AudioEditorWidget.h"

#include <algorithm>
#include <cmath>

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../hw/HardwareState.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/GroupManager.h"
#include "../../engine/SamplePreviewPlayer.h"
#include "../../engine/SamplerInstrument.h"
#include "../../engine/commands/NormalizeSampleCommand.h"
#include "../../input/InputEvent.h"
#include "../../input/InputManager.h"
#include "../views/operations/EditorOperation.h"
#include "../views/operations/SampleRangeOperation.h"
#include "../views/operations/NormalizeOperation.h"
#include "../views/operations/SliceOperation.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"
#include "ConfirmDialog.h"
#include "WindowManager.h"

namespace
{
constexpr double kMinRangeSeconds = 0.001;
constexpr int kRangeContextPixels = 10;
constexpr int kDefaultWaveformWidth = UiTheme::kPanelWidth - 2 * UiTheme::kPadding;
} // namespace

AudioEditorWidget::AudioEditorWidget()
    : sampleRangeOp_(std::make_unique<SampleRangeOperation>())
    , normalizeOp_(std::make_unique<NormalizeOperation>())
    , sliceOp_(std::make_unique<SliceOperation>())
{
    thumbnailFormatManager_.registerBasicFormats();
}

AudioEditorWidget::~AudioEditorWidget() = default;

// -- Widget identity --

WidgetDescriptor AudioEditorWidget::describe() const
{
    return { "audio_editor", 1, false, DisplayConstraint::Any };
}

juce::String AudioEditorWidget::getTitleSubtitle() const
{
    if (!hasSample_)
        return modeName() + "  ·  No sample";
    juce::String s;
    s << modeName()
      << "  ·  Pad " << (currentPadIndex_ + 1)
      << "  ·  " << formatTimeString(getTotalSampleSeconds());
    return s;
}

juce::Colour AudioEditorWidget::getAccentColour() const
{
    return cachedAccent_;
}

// -- Lifecycle --

void AudioEditorWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    tickDiv_ = 0;
    hw().setLed("sampling", HardwareConstants::kLedBright,
                describe().id.toStdString());

    if (previewPlayer_ == nullptr)
        previewPlayer_ = std::make_unique<SamplePreviewPlayer>(engine());
    if (sliceOp_ != nullptr)
        sliceOp_->setPreview(previewPlayer_.get());
    if (!listeningForPadTriggers_)
    {
        engine().getSampler().addListener(this);
        listeningForPadTriggers_ = true;
    }

    refreshState();
    activateCurrentOperation();
    updateManualInputClaim();
}

void AudioEditorWidget::onDeactivated()
{
    if (listeningForPadTriggers_)
    {
        engine().getSampler().removeListener(this);
        listeningForPadTriggers_ = false;
    }
    rangePlayheadActive_ = false;
    rangePlayheadSeconds_ = 0.0;

    if (sliceDetailsVisible_)
    {
        sliceDetailsVisible_ = false;
        if (onSliceDetailsVisibilityChanged_)
            onSliceDetailsVisibilityChanged_(false);
    }
    releaseRangeViewport();
    releaseManualInput();

    EditorOperation* op = getActiveOperation();
    if (op != nullptr)
        op->deactivate();

    if (sliceOp_ != nullptr)
        sliceOp_->setPreview(nullptr);
    if (previewPlayer_ != nullptr)
    {
        previewPlayer_->stop();
        previewPlayer_.reset();
    }
}

void AudioEditorWidget::onActiveSamplerAboutToChange()
{
    if (listeningForPadTriggers_)
    {
        engine().getSampler().removeListener(this);
        listeningForPadTriggers_ = false;
    }
}

void AudioEditorWidget::onActiveSamplerChanged()
{
    if (!listeningForPadTriggers_)
    {
        engine().getSampler().addListener(this);
        listeningForPadTriggers_ = true;
    }
    refreshState();
}

// -- Resources --

std::vector<std::string> AudioEditorWidget::requiredResources(int page)
{
    if (page != 0)
        return {};

    std::vector<std::string> resources;

    // Screen button LED
    resources.push_back("sampling");
    resources.push_back("arrowLeft");
    resources.push_back("arrowRight");

    if (panelOffset_ == 0)
    {
        for (int i = 1; i <= 4; ++i)
        {
            resources.push_back("d" + std::to_string(i));
            resources.push_back("k" + std::to_string(i));
        }
    }
    else
    {
        for (int i = 5; i <= 8; ++i)
        {
            resources.push_back("d" + std::to_string(i));
            resources.push_back("k" + std::to_string(i));
        }
    }

    // Pad LEDs (p1..p16) for Manual slice feedback are claimed/released
    // directly via updateManualInputClaim() — not listed here, because
    // requiredResources() is only re-queried on activation/page scroll, and
    // Manual mode is entered mid-session via the K0 knob.

    return resources;
}

// -- Options & Knobs --

std::vector<Option> AudioEditorWidget::getOptions(int page)
{
    if (page != 0)
        return {};
    return currentOptions_;
}

std::vector<Knob> AudioEditorWidget::getKnobs(int page)
{
    if (page != 0)
        return {};
    return currentKnobs_;
}

// -- Rendering --

void AudioEditorWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void AudioEditorWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (page != 0)
        return;

    // Title + subtitle are rendered by WindowManager via getTitle/getTitleSubtitle.
    auto contentArea = bounds.reduced(UiTheme::kPadding);
    auto waveformArea = contentArea;

    if (!hasSample_)
    {
        g.setColour(UiTheme::kTextSecondary);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
        g.drawFittedText("Load or select a sampled pad to edit its range.",
                         waveformArea,
                         juce::Justification::centred, 2);
        return;
    }

    // Waveform background — sharp-cornered panel matching Pad Details.
    g.setColour(UiTheme::kOptionFillEmpty);
    g.fillRect(waveformArea);
    g.setColour(UiTheme::kOptionBorderEmpty);
    g.drawRect(waveformArea, 1);

    // Range has an explicit K4 zoom that blends from full-file context to a
    // tight fit around Start/End. Other operations always work inside the
    // selected sample window.
    const double fileDuration = juce::jmax(getTotalSampleSeconds(), kMinRangeSeconds);
    const bool rangeOperation = currentOperation_ == Operation::SampleRange;
    const auto rangeView = getRangeVisibleWindow(waveformArea.getWidth());
    const double visibleStart = rangeOperation ? rangeView.startSeconds
                                               : rangeStartSeconds_;
    const double visibleEnd = rangeOperation
        ? rangeView.endSeconds
        : juce::jmax(rangeEndSeconds_, rangeStartSeconds_ + kMinRangeSeconds);
    const double visibleSpan  = juce::jmax(visibleEnd - visibleStart, kMinRangeSeconds);

    // Draw waveform using the active group colour for consistency with Pad Details.
    if (thumbnail_.getTotalLength() > 0.0)
    {
        g.setColour(getAccentColour().withAlpha(0.85f));
        thumbnail_.drawChannels(g, waveformArea.reduced(4),
                                visibleStart, visibleEnd, 1.0f);
    }

    // Delegate overlay rendering to the active operation using the same
    // visible time window as the waveform.
    if (rangeOperation && sampleRangeOp_ != nullptr)
        sampleRangeOp_->setVisibleRange(visibleStart, visibleEnd);

    const double totalSeconds = rangeOperation ? fileDuration : visibleSpan;
    EditorOperation* op = getActiveOperation();
    if (op != nullptr)
        op->paintOverlay(g, waveformArea, totalSeconds);

    if (rangeOperation && rangePlayheadActive_
        && rangePlayheadSeconds_ >= visibleStart
        && rangePlayheadSeconds_ <= visibleEnd)
    {
        const float playheadX = static_cast<float>(waveformArea.getX())
            + static_cast<float>((rangePlayheadSeconds_ - visibleStart) / visibleSpan)
                * static_cast<float>(waveformArea.getWidth());
        g.setColour(juce::Colours::white);
        g.drawLine(playheadX,
                   static_cast<float>(waveformArea.getY()),
                   playheadX,
                   static_cast<float>(waveformArea.getBottom()),
                   1.5f);
    }
}

void AudioEditorWidget::resized()
{
    // No child components to layout
}

// -- Input --

void AudioEditorWidget::handleKnob(int localIndex, int16_t delta, uint16_t /*absolute*/, bool shift)
{
    if (localIndex < 0 || localIndex >= static_cast<int>(currentKnobs_.size()))
        return;

    auto& knob = currentKnobs_[static_cast<size_t>(localIndex)];
    if (!knob.isEnabled)
        return;

    if (currentOperation_ == Operation::SampleRange
        && (localIndex == 0 || localIndex == 1))
    {
        freezeRangeViewport();
    }

    if (auto* numModel = std::get_if<Knob::NumericModel>(&knob.model))
    {
        const double stepMultiplier = shift ? 0.1 : 1.0;
        double newValue = numModel->value + static_cast<double>(delta) * numModel->step * stepMultiplier;
        newValue = juce::jlimit(numModel->minimum, numModel->maximum, newValue);
        numModel->value = newValue;

        if (numModel->onChange)
            numModel->onChange(newValue);
    }
    else if (auto* listModel = std::get_if<Knob::ListModel>(&knob.model))
    {
        listKnobAccum_[static_cast<size_t>(localIndex)] += delta;
        const int unit = UiTheme::kMediumKnobTicks;
        if (std::abs(listKnobAccum_[static_cast<size_t>(localIndex)]) < unit)
            return;
        const int step = listKnobAccum_[static_cast<size_t>(localIndex)] > 0 ? 1 : -1;
        listKnobAccum_[static_cast<size_t>(localIndex)] = 0;

        const int count = static_cast<int>(listModel->entries.size());
        if (count <= 0) return;
        int newIndex = juce::jlimit(0, count - 1, listModel->selectedIndex + step);
        if (newIndex == listModel->selectedIndex) return;
        listModel->selectedIndex = newIndex;

        if (listModel->onChange)
            listModel->onChange(newIndex);
    }
}

void AudioEditorWidget::handleOption(int localIndex)
{
    if (localIndex < 0 || localIndex >= static_cast<int>(currentOptions_.size()))
        return;

    auto& opt = currentOptions_[static_cast<size_t>(localIndex)];
    if (opt.state == OptionState::Disabled || opt.state == OptionState::Empty)
        return;

    if (opt.isOperationNavigator && opt.onOperationNavigatorPart)
    {
        // Toggle direction based on current state (next on press)
        opt.onOperationNavigatorPart(true);
    }
    else if (opt.onInvoke)
    {
        opt.onInvoke();
    }
}

void AudioEditorWidget::handleButton(const controller_events::ButtonEvent& e)
{
    if (!e.pressed
        || currentOperation_ != Operation::Slice
        || sliceOp_ == nullptr
        || sliceOp_->getType() != SliceOperation::Type::Manual)
        return;

    int direction = 0;
    if (e.name == "navLeft" || e.name == "arrowLeft")
        direction = -1;
    else if (e.name == "navRight" || e.name == "arrowRight")
        direction = 1;

    if (direction != 0 && sliceOp_->selectAdjacentManualSlice(direction))
    {
        rebuildKnobs();
        repaint();
    }
}

// -- Mode & Operation --

void AudioEditorWidget::setMode(Mode newMode)
{
    if (mode_ == newMode)
        return;

    mode_ = newMode;
    rebuildOptions();
    rebuildKnobs();
    repaint();
}

void AudioEditorWidget::nextOperation()
{
    int current = static_cast<int>(currentOperation_);
    current = (current + 1);
    if (current > static_cast<int>(Operation::Slice))
        current = 0;
    currentOperation_ = static_cast<Operation>(current);
    activateCurrentOperation();
    rebuildOptions();
    rebuildKnobs();
    repaint();
}

void AudioEditorWidget::prevOperation()
{
    int current = static_cast<int>(currentOperation_);
    current = (current - 1);
    if (current < 0)
        current = static_cast<int>(Operation::Slice);
    currentOperation_ = static_cast<Operation>(current);
    activateCurrentOperation();
    rebuildOptions();
    rebuildKnobs();
    repaint();
}

// -- Tick --

void AudioEditorWidget::onUiHostTick()
{
    ++manualLedBlinkPhase_;
    if (sliceOp_ != nullptr
        && currentOperation_ == Operation::Slice)
    {
        // Every proposed slice auditions only its own window. This prevents a
        // pad tap from spilling into the next chop before Apply.
        const bool playheadWasVisible = previewPlayer_ != nullptr
                                     && previewPlayer_->isPlaying();
        const double previewEnd = sliceOp_->getAuditionEndSeconds();
        if (playheadWasVisible
            && previewEnd > 0.0
            && previewPlayer_->getPositionSeconds() >= previewEnd)
        {
            previewPlayer_->stop();
        }

        if (sliceOp_->getType() == SliceOperation::Type::Manual)
            publishManualPadLeds();
        // The LED blink is hardware-only. Repaint while the preview playhead
        // is visible (including the tick that removes it), but keep an idle
        // slice screen out of the display queue.
        if (playheadWasVisible)
            repaint();
    }

    if (rangePlayheadActive_)
    {
        const double elapsedSeconds =
            (juce::Time::getMillisecondCounterHiRes() - rangePlayheadStartedMs_) / 1000.0;
        rangePlayheadSeconds_ = rangePlayheadStartSeconds_ + elapsedSeconds;
        if (rangePlayheadSeconds_ >= rangePlayheadEndSeconds_)
        {
            rangePlayheadSeconds_ = rangePlayheadEndSeconds_;
            rangePlayheadActive_ = false;
        }
        repaint();
    }

    if (++tickDiv_ < 6) return;
    tickDiv_ = 0;
    const auto previousSignature = computeVisualSignature();
    refreshState();
    if (computeVisualSignature() != previousSignature)
        repaint();
}

void AudioEditorWidget::padTriggered(int padIndex)
{
    if (currentOperation_ != Operation::SampleRange
        || !hasSample_
        || padIndex != currentPadIndex_)
        return;

    rangePlayheadStartSeconds_ = rangeStartSeconds_;
    rangePlayheadEndSeconds_ = juce::jmax(
        rangePlayheadStartSeconds_ + kMinRangeSeconds,
        rangeEndSeconds_);
    rangePlayheadSeconds_ = rangePlayheadStartSeconds_;
    rangePlayheadStartedMs_ = juce::Time::getMillisecondCounterHiRes();
    rangePlayheadActive_ = true;
    repaint();
}

void AudioEditorWidget::padStopped(int padIndex)
{
    if (padIndex != currentPadIndex_ || !rangePlayheadActive_)
        return;

    rangePlayheadActive_ = false;
    repaint();
}

// -- State refresh --

std::size_t AudioEditorWidget::computeVisualSignature() const
{
    std::size_t signature = 0;
    const auto mix = [&signature](std::size_t value)
    {
        signature ^= value + 0x9e3779b97f4a7c15ULL
                   + (signature << 6) + (signature >> 2);
    };

    mix(static_cast<std::size_t>(currentPadId_ + 1));
    mix(static_cast<std::size_t>(currentPadIndex_ + 1));
    mix(static_cast<std::size_t>(hasSample_));
    mix(static_cast<std::size_t>(currentSnapshot_.sampleFile.getFullPathName().hashCode64()));
    mix(std::hash<double>{}(currentSnapshot_.totalLengthSeconds));
    mix(std::hash<double>{}(currentSnapshot_.windowStartSeconds));
    mix(std::hash<double>{}(currentSnapshot_.windowEndSeconds));
    mix(std::hash<float>{}(currentSnapshot_.gainDb));
    mix(static_cast<std::size_t>(cachedAccent_.getARGB()));
    return signature;
}

void AudioEditorWidget::refreshState()
{
    auto& padBank = engine().getSampler();
    int selectedId = padBank.getSelectedPad();

    // Refresh cached accent from active group colour.
    auto& gm = engine().getGroupManager();
    const int ag = gm.getActiveGroupIndex();
    cachedAccent_ = (ag >= 0)
        ? UiTheme::ledIndexToColour(UiTheme::brightestHueVariant(gm.getGroupColor(ag)))
        : UiTheme::kTitlebarAccent;

    auto padSnapshots = padBank.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::State);
    if (selectedId < 0 && !padSnapshots.empty())
        selectedId = padSnapshots.front().id;

    auto iter = std::find_if(padSnapshots.begin(), padSnapshots.end(),
                             [selectedId](const SamplerInstrument::PadSnapshot& s)
                             { return s.id == selectedId; });

    hasSample_ = iter != padSnapshots.end() && iter->hasSample;
    currentPadId_ = hasSample_ ? iter->id : selectedId;
    currentPadIndex_ = hasSample_
                           ? static_cast<int>(std::distance(padSnapshots.begin(), iter))
                           : -1;

    if (!hasSample_)
    {
        currentSnapshot_ = SamplerInstrument::PadSnapshot{};
        thumbnail_.clear();
        thumbnailFile_ = juce::File();
        rebuildOptions();
        rebuildKnobs();
        return;
    }

    currentSnapshot_ = *iter;

    const double totalSeconds = getTotalSampleSeconds();

    if (rangeZoomPadId_ != currentSnapshot_.id)
    {
        rangeZoomPadId_ = currentSnapshot_.id;
        rangeZoom_ = 0.0;
    }

    rangeStartSeconds_ = currentSnapshot_.windowStartSeconds;
    if (!std::isfinite(rangeStartSeconds_) || rangeStartSeconds_ < 0.0)
        rangeStartSeconds_ = 0.0;

    double maxEnd = totalSeconds > 0.0
                        ? totalSeconds
                        : juce::jmax(rangeStartSeconds_ + kMinRangeSeconds, currentSnapshot_.windowEndSeconds);
    if (maxEnd < rangeStartSeconds_ + kMinRangeSeconds)
        maxEnd = rangeStartSeconds_ + kMinRangeSeconds;

    double requestedEnd = currentSnapshot_.windowEndSeconds;
    if (!std::isfinite(requestedEnd) || requestedEnd <= rangeStartSeconds_)
        requestedEnd = maxEnd;

    rangeStartSeconds_ = juce::jlimit(0.0, juce::jmax(0.0, maxEnd - kMinRangeSeconds), rangeStartSeconds_);
    rangeEndSeconds_ = juce::jlimit(rangeStartSeconds_ + kMinRangeSeconds,
                                     juce::jmax(rangeStartSeconds_ + kMinRangeSeconds, maxEnd),
                                     requestedEnd);

    // Refresh operation state
    EditorOperation* op = getActiveOperation();
    if (op != nullptr)
        op->refreshFromSnapshot(currentSnapshot_);

    // Update thumbnail if needed
    if (currentSnapshot_.sampleFile.existsAsFile()
        && (thumbnailFile_ != currentSnapshot_.sampleFile
            || thumbnail_.getTotalLength() <= 0.0))
    {
        thumbnail_.clear();
        thumbnailFile_ = currentSnapshot_.sampleFile;
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            thumbnailFormatManager_.createReaderFor(currentSnapshot_.sampleFile));
        if (reader != nullptr)
            // AudioThumbnail::setSource takes ownership — make the transfer explicit.
            thumbnail_.setSource(std::make_unique<juce::FileInputSource>(currentSnapshot_.sampleFile).release());
    }

    rebuildOptions();
    rebuildKnobs();
}

void AudioEditorWidget::rebuildOptions()
{
    currentOptions_.clear();

    if (mode_ == Mode::Sampling && hasSample_)
    {
        // Operation navigator (prev/next)
        Option operationNav;
        operationNav.id = "operation.navigator";
        operationNav.label = operationName(currentOperation_);
        operationNav.span = 2;
        operationNav.isOperationNavigator = true;
        operationNav.state = OptionState::Enabled;
        operationNav.onOperationNavigatorPart = [this](bool isLeftPart)
        {
            if (isLeftPart)
                prevOperation();
            else
                nextOperation();
        };
        currentOptions_.push_back(std::move(operationNav));

        // Spacer
        Option spacer;
        spacer.id = "operation.spacer";
        spacer.label = "";
        spacer.state = OptionState::Empty;
        currentOptions_.push_back(std::move(spacer));

        // Apply button
        Option applyOption;
        applyOption.id = "operation.apply";
        applyOption.label = "Apply";
        applyOption.state = (hasSample_ && currentPadId_ >= 0) ? OptionState::Enabled : OptionState::Disabled;
        applyOption.onInvoke = [this]()
        {
            auto* edit = engine().getEdit();
            if (edit == nullptr || currentPadId_ < 0)
                return;

            EditorOperation* op = getActiveOperation();
            if (op == nullptr || !op->canApply())
                return;

            op->apply(engine(), currentPadId_, edit->getUndoManager());

            // Feedback toast for the Slice op — Success on a clean apply,
            // Error if the command reported failure (source missing, write
            // failed, etc.). ASCII arrow to avoid Unicode font-coverage risk
            // on the embedded display.
            if (currentOperation_ == Operation::Slice && sliceOp_ != nullptr)
            {
                if (sliceOp_->didLastApplyFail())
                {
                    showToast(ToastKind::Error, "Slice failed — see log");
                }
                else
                {
                    const int count = sliceOp_->getLastAppliedSliceCount();
                    showToast(ToastKind::Success,
                              juce::String(count) + " slices -> pads 1-" + juce::String(count));
                    auto continueToSequencer = onSlicesApplied_;
                    if (continueToSequencer)
                        continueToSequencer(count);
                }
            }
        };
        currentOptions_.push_back(std::move(applyOption));
    }
}

void AudioEditorWidget::rebuildKnobs()
{
    currentKnobs_.clear();
    updateSliceDetailsVisibility();

    if (!hasSample_ || mode_ != Mode::Sampling)
    {
        // Fill 4 empty disabled knobs
        for (int i = 0; i < 4; ++i)
        {
            Knob k;
            k.id = juce::String(i);
            k.isEnabled = false;
            currentKnobs_.push_back(std::move(k));
        }
        updateManualInputClaim();
        return;
    }

    // Operations after range selection provide their own editing controls.
    // Previously Normalize accidentally kept showing the range knobs, making
    // its target and even-factor settings unreachable from hardware.
    if (currentOperation_ == Operation::Normalize && normalizeOp_ != nullptr)
    {
        const double totalSeconds = juce::jmax(getTotalSampleSeconds(), kMinRangeSeconds);
        currentKnobs_ = normalizeOp_->getKnobs(totalSeconds);
        updateManualInputClaim();
        return;
    }

    if (currentOperation_ == Operation::Slice && sliceOp_ != nullptr)
    {
        const double totalSeconds = juce::jmax(getTotalSampleSeconds(), kMinRangeSeconds);
        currentKnobs_ = sliceOp_->getKnobs(totalSeconds);
        updateManualInputClaim();
        return;
    }

    const double totalSeconds = juce::jmax(getTotalSampleSeconds(), kMinRangeSeconds);
    const int waveformWidth = juce::jmax(1, getWidth() > 0
        ? getWidth() - 2 * UiTheme::kPadding
        : kDefaultWaveformWidth);
    const auto visibleRange = getRangeVisibleWindow(waveformWidth);
    const double visibleSpan = juce::jmax(kMinRangeSeconds,
                                          visibleRange.endSeconds - visibleRange.startSeconds);
    const double trimStepSeconds = visibleSpan / static_cast<double>(waveformWidth);

    // Start knob
    Knob startKnob;
    startKnob.id = "range.start";
    startKnob.label = "Start";
    startKnob.isEnabled = true;
    startKnob.continuousMode = true;
    startKnob.onInputResolved = [this](double) { releaseRangeViewport(); };

    Knob::NumericModel startModel;
    startModel.value = rangeStartSeconds_;
    startModel.minimum = 0.0;
    startModel.maximum = totalSeconds;
    startModel.step = trimStepSeconds;
    startModel.formatter = [](double s) { return AudioEditorWidget::formatTimeString(s); };
    startModel.onChange = [this](double newValue)
    {
        const double maxStart = juce::jmax(0.0, rangeEndSeconds_ - kMinRangeSeconds);
        rangeStartSeconds_ = juce::jlimit(0.0, maxStart, newValue);
        engine().getSampler().setPadSampleRange(currentPadId_, rangeStartSeconds_, rangeEndSeconds_);
    };
    startKnob.model = startModel;
    currentKnobs_.push_back(std::move(startKnob));

    // End knob
    Knob endKnob;
    endKnob.id = "range.end";
    endKnob.label = "End";
    endKnob.isEnabled = true;
    endKnob.continuousMode = true;
    endKnob.onInputResolved = [this](double) { releaseRangeViewport(); };

    Knob::NumericModel endModel;
    endModel.value = rangeEndSeconds_;
    endModel.minimum = 0.0;
    endModel.maximum = totalSeconds;
    endModel.step = trimStepSeconds;
    endModel.formatter = [](double s) { return AudioEditorWidget::formatTimeString(s); };
    endModel.onChange = [this](double newValue)
    {
        const double minEnd = rangeStartSeconds_ + kMinRangeSeconds;
        const double maxEnd = getTotalSampleSeconds() > 0.0 ? getTotalSampleSeconds() : rangeEndSeconds_;
        rangeEndSeconds_ = juce::jlimit(minEnd, maxEnd, newValue);
        engine().getSampler().setPadSampleRange(currentPadId_, rangeStartSeconds_, rangeEndSeconds_);
    };
    endKnob.model = endModel;
    currentKnobs_.push_back(std::move(endKnob));

    Knob placeholder;
    placeholder.id = "2";
    placeholder.isEnabled = false;
    currentKnobs_.push_back(std::move(placeholder));

    Knob zoomKnob;
    zoomKnob.id = "range.zoom";
    zoomKnob.label = "Zoom";
    zoomKnob.isEnabled = true;
    zoomKnob.continuousMode = true;

    Knob::NumericModel zoomModel;
    zoomModel.value = rangeZoom_;
    zoomModel.minimum = 0.0;
    zoomModel.maximum = 1.0;
    zoomModel.step = 0.002;
    zoomModel.formatter = [](double zoom)
    {
        if (zoom <= 0.001)
            return juce::String("Full");
        return juce::String(static_cast<int>(std::round(zoom * 100.0))) + "%";
    };
    zoomModel.onChange = [this](double newValue)
    {
        rangeZoom_ = juce::jlimit(0.0, 1.0, newValue);
        repaint();
    };
    zoomKnob.model = zoomModel;
    currentKnobs_.push_back(std::move(zoomKnob));

    updateManualInputClaim();
}

AudioEditorWidget::SliceDetailsState AudioEditorWidget::getSliceDetailsState() const
{
    SliceDetailsState state;
    state.active = currentOperation_ == Operation::Slice
        && sliceOp_ != nullptr
        && sliceOp_->getType() == SliceOperation::Type::Manual;
    if (!state.active)
        return state;

    state.overlapping = sliceOp_->isOverlapping();
    state.selectedSlice = sliceOp_->getActiveSliceIndex();
    for (const auto& range : sliceOp_->getManualSliceRanges())
        state.ranges.push_back({ range.startSeconds, range.endSeconds });
    return state;
}

bool AudioEditorWidget::selectManualSlice(int sliceIndex)
{
    if (currentOperation_ != Operation::Slice
        || sliceOp_ == nullptr
        || sliceOp_->getType() != SliceOperation::Type::Manual
        || !sliceOp_->selectManualSlice(sliceIndex))
        return false;

    rebuildKnobs();
    repaint();
    return true;
}

void AudioEditorWidget::setManualSliceOverlapping(bool overlapping)
{
    if (currentOperation_ != Operation::Slice
        || sliceOp_ == nullptr
        || sliceOp_->getType() != SliceOperation::Type::Manual)
        return;

    sliceOp_->setOverlapping(overlapping);
    rebuildKnobs();
    repaint();
}

void AudioEditorWidget::updateSliceDetailsVisibility()
{
    const bool shouldShow = hasSample_
        && currentOperation_ == Operation::Slice
        && sliceOp_ != nullptr
        && sliceOp_->getType() == SliceOperation::Type::Manual;
    if (shouldShow == sliceDetailsVisible_)
        return;

    sliceDetailsVisible_ = shouldShow;
    if (onSliceDetailsVisibilityChanged_)
        onSliceDetailsVisibilityChanged_(shouldShow);
}

// -- Operation management --

EditorOperation* AudioEditorWidget::getActiveOperation() const
{
    switch (currentOperation_)
    {
        case Operation::SampleRange:
            return sampleRangeOp_.get();
        case Operation::Normalize:
            return normalizeOp_.get();
        case Operation::Slice:
            return sliceOp_.get();
    }
    return nullptr;
}

void AudioEditorWidget::activateCurrentOperation()
{
    if (currentOperation_ != Operation::SampleRange)
        releaseRangeViewport();

    // Deactivate all first
    if (sampleRangeOp_)
        sampleRangeOp_->deactivate();
    if (normalizeOp_)
        normalizeOp_->deactivate();
    if (sliceOp_)
        sliceOp_->deactivate();

    // Activate the current operation so its audioEngine pointer is set.
    // All three operations' canApply() checks `audioEngine != nullptr`, so
    // without this call Apply silently no-ops for every op. An earlier
    // revision of this method bypassed activation (see git history) — that
    // was a regression from the Widget-architecture migration.
    if (auto* op = getActiveOperation())
    {
        op->activate(hasAudioEngine() ? &engine() : nullptr);
        if (hasAudioEngine() && hasSample_)
            op->refreshFromSnapshot(currentSnapshot_);
    }

    if (sliceOp_ != nullptr)
    {
        sliceOp_->setOnEraseAll([this]() {
            auto* wm = windowManager();
            if (wm == nullptr) return;
            const int count = static_cast<int>(sliceOp_->getCapturedStarts().size());
            if (count == 0) return;  // nothing to erase

            auto dialog = std::make_unique<ConfirmDialog>(
                juce::String("Erase slices?"),
                juce::String("Clear all ") + juce::String(count) + juce::String(" captured boundaries."),
                juce::String("Erase"),
                [this]() {
                    if (sliceOp_ != nullptr) sliceOp_->confirmEraseAll();
                    repaint();
                });
            wm->showDialog(std::move(dialog));
        });
    }

    updateManualInputClaim();
}

// -- Helpers --

juce::String AudioEditorWidget::operationName(Operation op) const
{
    switch (op)
    {
        case Operation::SampleRange: return "Range";
        case Operation::Normalize:   return "Normalize";
        case Operation::Slice:       return "Slice";
    }
    return "Unknown";
}

juce::String AudioEditorWidget::modeName() const
{
    switch (mode_)
    {
        case Mode::Sampling: return "Sampling";
    }
    return "Unknown";
}

double AudioEditorWidget::getTotalSampleSeconds() const
{
    return currentSnapshot_.totalLengthSeconds;
}

AudioEditorWidget::VisibleRange AudioEditorWidget::getRangeVisibleWindow(
    int waveformWidth) const
{
    if (rangeViewportFrozen_)
        return frozenRangeView_;

    const double fileDuration = juce::jmax(getTotalSampleSeconds(), kMinRangeSeconds);
    const double selectionStart = juce::jlimit(
        0.0, juce::jmax(0.0, fileDuration - kMinRangeSeconds),
        rangeStartSeconds_);
    const double selectionEnd = juce::jlimit(
        selectionStart + kMinRangeSeconds, fileDuration,
        juce::jmax(rangeEndSeconds_, selectionStart + kMinRangeSeconds));
    const double selectionSpan = juce::jmax(kMinRangeSeconds,
                                            selectionEnd - selectionStart);

    const int contentPixels = juce::jmax(
        1, waveformWidth - 2 * kRangeContextPixels);
    const double contextSeconds = selectionSpan
        * static_cast<double>(kRangeContextPixels)
        / static_cast<double>(contentPixels);

    const double tightStart = selectionStart > 0.0
        ? juce::jmax(0.0, selectionStart - contextSeconds)
        : 0.0;
    const double tightEnd = selectionEnd < fileDuration
        ? juce::jmin(fileDuration, selectionEnd + contextSeconds)
        : fileDuration;
    const double zoom = juce::jlimit(0.0, 1.0, rangeZoom_);

    return {
        tightStart * zoom,
        fileDuration + (tightEnd - fileDuration) * zoom
    };
}

void AudioEditorWidget::freezeRangeViewport()
{
    if (rangeViewportFrozen_)
        return;

    const int waveformWidth = juce::jmax(1, getWidth() > 0
        ? getWidth() - 2 * UiTheme::kPadding
        : kDefaultWaveformWidth);
    frozenRangeView_ = getRangeVisibleWindow(waveformWidth);
    rangeViewportFrozen_ = true;
}

void AudioEditorWidget::releaseRangeViewport()
{
    if (!rangeViewportFrozen_)
        return;

    rangeViewportFrozen_ = false;
    rebuildKnobs();
    repaint();
}

juce::String AudioEditorWidget::formatTimeString(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        seconds = 0.0;

    const int totalMs = static_cast<int>(std::round(seconds * 1000.0));
    const int mins = totalMs / 60000;
    const int secs = (totalMs / 1000) % 60;
    const int ms = totalMs % 1000;

    return juce::String::formatted("%d:%02d.%03d", mins, secs, ms);
}

// -- Manual-mode InputManager claim/release --

void AudioEditorWidget::claimManualInput()
{
    updateManualPadLedClaim();

    if (manualPadBinding_ != kInvalidBinding) return;
    auto* host = controllerHost();
    if (host == nullptr) return;
    auto* im = host->getInputManager();
    if (im == nullptr) return;

    // Capture via SafePointer — the widget may be deactivated (and the
    // input binding removed) before a queued callAsync actually fires.
    juce::Component::SafePointer<AudioEditorWidget> safe(this);

    manualPadBinding_ = im->addPadHandler(
        InputManager::HandlerPriority::View, "",
        [safe](InputEvent& e) {
            const int padIdx = e.padIndex();
            if (e.isPressed() && padIdx >= 0)
            {
                const bool macro = e.metadata.contains("macro")
                    && static_cast<bool>(e.metadata["macro"]);
                const bool undo = (e.isShift() || macro) && padIdx == 0;
                const bool redo = (e.isShift() || macro) && padIdx == 1;
                juce::MessageManager::callAsync([safe, padIdx, undo, redo] {
                    if (safe == nullptr) return;
                    if (safe->sliceOp_ != nullptr)
                    {
                        if (safe->sliceOp_->getType() == SliceOperation::Type::Manual
                            && undo)
                            safe->sliceOp_->undoManualEdit();
                        else if (safe->sliceOp_->getType() == SliceOperation::Type::Manual
                                 && redo)
                            safe->sliceOp_->redoManualEdit();
                        else
                            safe->sliceOp_->onPadPressed(padIdx);
                    }
                    safe->repaint();
                });
            }
            e.consumed = true;
        });

    manualEraseBinding_ = im->addButtonHandler(
        InputManager::HandlerPriority::View, "", juce::String("eraseReplace"),
        [safe](InputEvent& e) {
            const bool pressed = e.isPressed();
            juce::MessageManager::callAsync([safe, pressed] {
                if (safe == nullptr) return;
                if (safe->sliceOp_ != nullptr) safe->sliceOp_->onEraseEvent(pressed);
                safe->repaint();
            });
            e.consumed = true;
        });
}

void AudioEditorWidget::releaseManualInput()
{
    auto* host = controllerHost();
    if (host != nullptr)
    {
        if (auto* im = host->getInputManager())
        {
            if (manualPadBinding_   != kInvalidBinding) im->removeHandler(manualPadBinding_);
            if (manualEraseBinding_ != kInvalidBinding) im->removeHandler(manualEraseBinding_);
        }
    }
    manualPadBinding_   = kInvalidBinding;
    manualEraseBinding_ = kInvalidBinding;

    releaseManualPadLedClaim();

    // Ensure Manual op doesn't hold a stale erase-held flag.
    if (sliceOp_ != nullptr && sliceOp_->isEraseHeld())
        sliceOp_->onEraseEvent(false);
}

void AudioEditorWidget::updateManualInputClaim()
{
    const bool wantClaim =
        currentOperation_ == Operation::Slice
        && sliceOp_ != nullptr;
    if (wantClaim) claimManualInput();
    else           releaseManualInput();
}

void AudioEditorWidget::updateManualPadLedClaim()
{
    const bool wantPadLeds =
        currentOperation_ == Operation::Slice
        && sliceOp_ != nullptr
        && sliceOp_->getType() == SliceOperation::Type::Manual;

    if (!wantPadLeds)
    {
        releaseManualPadLedClaim();
        return;
    }

    if (manualPadLedsClaimed_ || !hasHardware())
        return;

    const auto owner = describe().id.toStdString();
    for (const auto& resource : ResourceIds::PadLeds)
    {
        if (hw().getOwner(resource) != owner)
            hw().forceRelease(resource, /*warn=*/false);
        hw().claim(resource, owner);
    }
    manualPadLedsClaimed_ = true;
    publishManualPadLeds();
}

void AudioEditorWidget::releaseManualPadLedClaim()
{
    if (!manualPadLedsClaimed_ || !hasHardware())
        return;

    const auto owner = describe().id.toStdString();
    for (const auto& resource : ResourceIds::PadLeds)
    {
        if (hw().getOwner(resource) == owner)
            hw().release(resource, owner);
    }
    manualPadLedsClaimed_ = false;

    // Manual slicing temporarily supersedes the pad-oriented widget on the
    // other panel. Restore that widget's normal claims and LED presentation.
    if (auto* wm = windowManager())
    {
        const auto otherSide = panelOffset_ == 0
            ? DisplaySide::Right
            : DisplaySide::Left;
        wm->reclaimResources(otherSide);
    }
}

void AudioEditorWidget::publishManualPadLeds()
{
    if (sliceOp_ == nullptr
        || currentOperation_ != Operation::Slice
        || sliceOp_->getType() != SliceOperation::Type::Manual)
        return;

    const auto owner = describe().id.toStdString();
    const int N = static_cast<int>(sliceOp_->getCapturedStarts().size());
    const bool eraseHeld = sliceOp_->isEraseHeld();
    const bool blinkOn = ((manualLedBlinkPhase_ / 8) & 1) == 0;

    for (int i = 0; i < 16; ++i)
    {
        const auto& resource = ResourceIds::PadLeds[static_cast<size_t>(i)];
        if (hw().getOwner(resource) != owner)
            continue;

        uint8_t color = HardwareConstants::kColorOff;
        if (i < N)
        {
            if (eraseHeld)
                color = HardwareConstants::kColorRedBright;
            else if (i == sliceOp_->getActiveSliceIndex())
                color = HardwareConstants::kColorGoldBright;
            else
                color = HardwareConstants::kColorGoldDim;
        }
        else if (i == N && !eraseHeld)
        {
            color = blinkOn ? HardwareConstants::kColorGoldBright
                            : HardwareConstants::kColorOff;
        }
        hw().setLed(resource, color, owner);
    }
}
