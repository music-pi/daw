#include "SliceOperation.h"

#include "../../../engine/AudioEngine.h"
#include "../../../engine/SamplePreviewPlayer.h"  // ISamplePreview
#include "../../../engine/TransientDetector.h"
#include "../../../engine/commands/SliceSampleCommand.h"
#include "../../theme/UiTheme.h"

#include <cmath>

//==============================================================================
// Lifecycle
//==============================================================================

void SliceOperation::activate(AudioEngine* engine)
{
    audioEngine = engine;
}

void SliceOperation::deactivate()
{
    // Operation navigation must also end the operation's transient playback
    // state. Previously a Slice audition survived the switch back to Range,
    // so the first proposed slice kept sounding under Range playback.
    if (preview_ != nullptr)
        preview_->stop();
    activeSliceIndex_ = -1;
    auditionEndSeconds_ = 0.0;
    capturing_ = false;
    audioEngine = nullptr;
}

//==============================================================================
// UI configuration
//==============================================================================

std::vector<Knob> SliceOperation::getKnobs(double /*totalSeconds*/)
{
    std::vector<Knob> knobs(4);

    // K0 — Type selector (Straight / Transient)
    Knob typeKnob;
    typeKnob.id = "slice.type";
    typeKnob.label = "Type";
    typeKnob.isEnabled = hasSample;
    typeKnob.continuousMode = false;

    Knob::ListModel typeModel;
    typeModel.entries = { "Straight", "Transient", "Manual" };
    typeModel.selectedIndex = (type_ == Type::Manual) ? 2
                            : (type_ == Type::Transient) ? 1 : 0;
    typeModel.onChange = [this](int newIndex)
    {
        Type next = Type::Straight;
        if (newIndex == 1) next = Type::Transient;
        else if (newIndex == 2) next = Type::Manual;

        if (next == type_) return;

        // Leaving Manual clears capture state (spec: state reset on K0 change away).
        if (type_ == Type::Manual)
        {
            capturedStarts_.clear();
            capturedEnds_.clear();
            overlapping_ = false;
            clearManualHistory();
            capturing_ = false;
            eraseHeld_ = false;
            erasePadActed_ = false;
            activeSliceIndex_ = -1;
        }

        if (preview_ != nullptr)
            preview_->stop();
        activeSliceIndex_ = -1;
        auditionEndSeconds_ = 0.0;

        type_ = next;
        if (type_ == Type::Transient)
            ensureAnalysisUpToDate();
        notifyThrottledUpdate();
    };
    typeKnob.model = typeModel;
    knobs[0] = std::move(typeKnob);

    // K1 — depends on type. Straight: slice count list. Transient: sensitivity.
    if (type_ == Type::Straight)
    {
        Knob countKnob;
        countKnob.id = "slice.count";
        countKnob.label = "Slices";
        countKnob.isEnabled = hasSample;
        countKnob.continuousMode = false;

        Knob::ListModel countModel;
        countModel.entries = { "4", "8", "16" };
        countModel.selectedIndex = [this]() {
            switch (sliceCount_) {
                case 4:  return 0;
                case 8:  return 1;
                case 16: default: return 2;
            }
        }();
        countModel.onChange = [this](int newIndex)
        {
            const int next = (newIndex == 0) ? 4 : (newIndex == 1) ? 8 : 16;
            if (next == sliceCount_) return;
            sliceCount_ = next;
            notifyThrottledUpdate();
        };
        countKnob.model = countModel;
        knobs[1] = std::move(countKnob);
    }
    else if (type_ == Type::Transient)
    {
        Knob sensKnob;
        sensKnob.id = "slice.sensitivity";
        sensKnob.label = "Sensitivity";
        sensKnob.isEnabled = hasSample;
        sensKnob.continuousMode = true;

        Knob::NumericModel sensModel;
        sensModel.value = sensitivity_;
        sensModel.minimum = 0.0;
        sensModel.maximum = 1.0;
        // Physical MK3 deltas can contain several raw units per detent.
        // Keep sensitivity deliberate enough to select a stable transient
        // count; AudioEditor applies a further 0.1 multiplier with Shift.
        sensModel.step = 0.002;
        sensModel.formatter = [](double v) {
            return juce::String(static_cast<int>(std::round(v * 100.0))) + "%";
        };
        sensModel.onChange = [this](double newValue)
        {
            const double clamped = juce::jlimit(0.0, 1.0, newValue);
            if (std::abs(clamped - sensitivity_) < 1e-6) return;
            sensitivity_ = clamped;
            ensureAnalysisUpToDate();
            notifyThrottledUpdate();
        };
        sensKnob.model = sensModel;
        knobs[1] = std::move(sensKnob);
    }
    else  // Manual
    {
        Knob k;
        k.id = "slice.manual.captured";
        k.label = "Press pads";
        k.isEnabled = false;
        Knob::NumericModel m;
        m.value = static_cast<double>(capturedStarts_.size());
        m.minimum = 0.0;
        m.maximum = 16.0;
        m.step = 1.0;
        m.formatter = [](double v) {
            return juce::String(static_cast<int>(v)) + "/16";
        };
        k.model = m;
        knobs[1] = std::move(k);

        // K2/K3: edit the selected slice's start and end. Interior
        // boundaries are shared, so moving End for slice K also moves Start
        // for slice K+1 and the slices remain contiguous.
        const bool haveActive = (activeSliceIndex_ >= 0
                                 && activeSliceIndex_ < static_cast<int>(capturedStarts_.size()));
        const int idx = activeSliceIndex_;
        const int N = static_cast<int>(capturedStarts_.size());

        Knob start;
        start.id = "slice.manual.start";
        start.label = "Start";
        start.continuousMode = true;
        start.isEnabled = haveActive;

        Knob::NumericModel startModel;
        if (haveActive)
        {
            startModel.value = capturedStarts_[static_cast<size_t>(idx)];
            if (overlapping_)
            {
                startModel.minimum = windowStartSeconds_;
                startModel.maximum =
                    capturedEnds_[static_cast<size_t>(idx)] - kMinSliceSeconds_;
            }
            else
            {
                startModel.minimum = (idx == 0)
                    ? windowStartSeconds_
                    : capturedStarts_[static_cast<size_t>(idx - 1)] + kMinSliceSeconds_;
                startModel.maximum =
                    capturedEnds_[static_cast<size_t>(idx)] - kMinSliceSeconds_;
            }
        }
        else
        {
            startModel.value = 0.0;
            startModel.minimum = 0.0;
            startModel.maximum = 0.0;
        }
        startModel.step = 0.001;
        startModel.formatter = [](double v) {
            return juce::String(v, 3) + juce::String("s");
        };
        startModel.onChange = [this](double newValue)
        {
            if (activeSliceIndex_ < 0
                || activeSliceIndex_ >= static_cast<int>(capturedStarts_.size()))
                return;
            if (std::abs(newValue
                         - capturedStarts_[static_cast<size_t>(activeSliceIndex_)]) < 1e-9)
                return;
            beginManualEdit();
            capturedStarts_[static_cast<size_t>(activeSliceIndex_)] = newValue;
            if (!overlapping_ && activeSliceIndex_ > 0)
                capturedEnds_[static_cast<size_t>(activeSliceIndex_ - 1)] = newValue;
            notifyThrottledUpdate();
        };
        start.model = startModel;
        knobs[2] = std::move(start);

        Knob end;
        end.id = "slice.manual.end";
        end.label = "End";
        end.continuousMode = true;
        end.isEnabled = haveActive;

        Knob::NumericModel endModel;
        if (haveActive)
        {
            endModel.value = capturedEnds_[static_cast<size_t>(idx)];
            endModel.minimum =
                capturedStarts_[static_cast<size_t>(idx)] + kMinSliceSeconds_;
            endModel.maximum = overlapping_ || idx + 1 >= N
                ? windowEndSeconds_
                : capturedEnds_[static_cast<size_t>(idx + 1)] - kMinSliceSeconds_;
        }
        else
        {
            endModel.value = haveActive ? windowEndSeconds_ : 0.0;
            endModel.minimum = endModel.value;
            endModel.maximum = endModel.value;
        }
        endModel.step = 0.001;
        endModel.formatter = [](double v) {
            return juce::String(v, 3) + juce::String("s");
        };
        endModel.onChange = [this](double newValue)
        {
            if (activeSliceIndex_ < 0
                || activeSliceIndex_ >= static_cast<int>(capturedEnds_.size()))
                return;
            if (std::abs(newValue
                         - capturedEnds_[static_cast<size_t>(activeSliceIndex_)]) < 1e-9)
                return;
            beginManualEdit();
            capturedEnds_[static_cast<size_t>(activeSliceIndex_)] = newValue;
            if (!overlapping_
                && activeSliceIndex_ + 1 < static_cast<int>(capturedStarts_.size()))
                capturedStarts_[static_cast<size_t>(activeSliceIndex_ + 1)] = newValue;
            notifyThrottledUpdate();
        };
        end.model = endModel;
        knobs[3] = std::move(end);

        return knobs;
    }

    // K2, K3 — unused placeholders (Straight/Transient only)
    for (int i = 2; i < 4; ++i)
    {
        Knob placeholder;
        placeholder.id = "unused." + juce::String(i);
        placeholder.label = {};
        placeholder.isEnabled = false;
        knobs[static_cast<size_t>(i)] = std::move(placeholder);
    }

    return knobs;
}

std::vector<Option> SliceOperation::getOptions()
{
    // Options are built by AudioEditorWidget::rebuildOptions() — return empty.
    return {};
}

//==============================================================================
// State management
//==============================================================================

void SliceOperation::refreshFromSnapshot(const SamplerInstrument::PadSnapshot& snapshot)
{
    const int prevPadId = currentPadId;
    const juce::File prevFile = currentSampleFile_;
    const double prevWinStart = windowStartSeconds_;
    const double prevWinEnd   = windowEndSeconds_;

    hasSample    = snapshot.hasSample;
    currentPadId = snapshot.id;
    currentSampleFile_ = snapshot.sampleFile;

    // Window set by the Sample Range step. Fall back to the whole file if
    // the snapshot has no meaningful window (e.g. fresh load).
    if (snapshot.windowEndSeconds > snapshot.windowStartSeconds)
    {
        windowStartSeconds_ = snapshot.windowStartSeconds;
        windowEndSeconds_   = snapshot.windowEndSeconds;
    }
    else
    {
        windowStartSeconds_ = 0.0;
        windowEndSeconds_   = snapshot.totalLengthSeconds;
    }

    const bool padChanged = (prevPadId != -1 && prevPadId != currentPadId);
    const bool fileChanged = (prevFile != juce::File{} && prevFile != currentSampleFile_);
    const bool windowChanged =
        std::abs(prevWinStart - windowStartSeconds_) > 1e-9 ||
        std::abs(prevWinEnd   - windowEndSeconds_)   > 1e-9;

    if (padChanged || fileChanged || windowChanged)
    {
        activeSliceIndex_ = -1;
        auditionEndSeconds_ = 0.0;
        if (preview_ != nullptr)
            preview_->stop();

        // Manual capture boundaries belong to one exact source window.
        if (type_ == Type::Manual)
        {
            capturedStarts_.clear();
            capturedEnds_.clear();
            clearManualHistory();
            capturing_ = false;
            eraseHeld_ = false;
            erasePadActed_ = false;
        }
    }

    if (type_ == Type::Transient && hasSample)
        ensureAnalysisUpToDate();
}

//==============================================================================
// Rendering
//==============================================================================

void SliceOperation::paintOverlay(juce::Graphics& g,
                                   const juce::Rectangle<int>& waveformArea,
                                   double totalSeconds)
{
    if (!hasSample)
        return;

    const float areaX     = static_cast<float>(waveformArea.getX());
    const float areaW     = static_cast<float>(waveformArea.getWidth());
    const float areaTop   = static_cast<float>(waveformArea.getY());
    const float areaBot   = static_cast<float>(waveformArea.getBottom());

    const auto tickColour = UiTheme::kAccentOrange.withAlpha(0.80f);

    // Build a list of normalised tick positions (0..1) from the active model.
    // The visible region is the Sample Range window — overlay math maps
    // seconds-within-window to 0..1 of waveformArea.
    std::vector<float> ticks;
    if (type_ == Type::Straight)
    {
        if (sliceCount_ <= 0) return;
        ticks.reserve(static_cast<size_t>(sliceCount_));
        for (int i = 1; i < sliceCount_; ++i)
            ticks.push_back(static_cast<float>(i) / static_cast<float>(sliceCount_));
    }
    else if (type_ == Type::Transient)
    {
        if (detectedStarts_.size() < 2 || totalSeconds <= 0.0) return;
        ticks.reserve(detectedStarts_.size());
        // Skip starts[0] (== windowStart, no tick at the left edge)
        for (size_t i = 1; i < detectedStarts_.size(); ++i)
        {
            const double relative = detectedStarts_[i] - windowStartSeconds_;
            ticks.push_back(static_cast<float>(relative / totalSeconds));
        }
    }
    else  // Manual
    {
        if (totalSeconds <= 0.0) return;
        if (capturedStarts_.empty()) return;
        if (capturedStarts_.size() >= 2)
        {
            ticks.reserve(capturedStarts_.size());
            for (size_t i = 1; i < capturedStarts_.size(); ++i)
            {
                const double relative = capturedStarts_[i] - windowStartSeconds_;
                ticks.push_back(static_cast<float>(relative / totalSeconds));
            }
        }
    }

    const int sliceCount = static_cast<int>(ticks.size()) + 1;
    const bool showLabels = (sliceCount <= 8);

    const juce::Font labelFont(juce::FontOptions(10.0f));
    g.setFont(labelFont);

    if (showLabels)
    {
        // The left edge is the start of the first slice. Boundary ticks mark
        // the starts of slices 2..N, so their labels must begin at 2.
        g.setColour(tickColour.withAlpha(0.70f));
        g.drawSingleLineText("1",
                             static_cast<int>(areaX + 3.0f),
                             static_cast<int>(areaTop + 11.0f));
    }

    for (size_t i = 0; i < ticks.size(); ++i)
    {
        const float x = areaX + ticks[i] * areaW;

        g.setColour(tickColour);
        g.drawLine(x, areaTop, x, areaBot, 1.5f);

        if (showLabels)
        {
            g.setColour(tickColour.withAlpha(0.70f));
            const juce::String label(static_cast<int>(i) + 2);
            const float labelX = x + 3.0f;
            const float labelY = areaTop + 3.0f;
            g.drawSingleLineText(label, static_cast<int>(labelX), static_cast<int>(labelY + 8.0f));
        }
    }

    // Manual mode: highlight the active tick in a brighter, thicker style.
    // ticks[i] corresponds to capturedStarts_[i+1], so the active tick index
    // in the ticks array is activeSliceIndex_ - 1. Only drawable when
    // activeSliceIndex_ >= 1 (index 0 sits at windowStart with no visible tick).
    if (type_ == Type::Manual && activeSliceIndex_ >= 1
        && static_cast<size_t>(activeSliceIndex_ - 1) < ticks.size())
    {
        const size_t idx = static_cast<size_t>(activeSliceIndex_ - 1);
        const float x = areaX + ticks[idx] * areaW;
        g.setColour(UiTheme::kAccentOrange.withMultipliedBrightness(1.4f));
        g.drawLine(x, areaTop, x, areaBot, 2.5f);
    }

    // Alternating tint for readability
    const auto tintColour = UiTheme::kAccentOrange.withAlpha(0.05f);
    std::vector<float> edges;
    edges.reserve(ticks.size() + 2);
    edges.push_back(0.0f);
    for (auto t : ticks) edges.push_back(t);
    edges.push_back(1.0f);
    for (size_t i = 0; i + 1 < edges.size(); i += 2)
    {
        const float x0 = areaX + edges[i] * areaW;
        const float x1 = areaX + edges[i + 1] * areaW;
        g.setColour(tintColour);
        g.fillRect(juce::Rectangle<float>(x0, areaTop, x1 - x0, areaBot - areaTop));
    }

    // Pad audition highlights the exact proposed slice, before Apply changes
    // any sampler state.
    if (type_ == Type::Manual
        && activeSliceIndex_ >= 0
        && activeSliceIndex_ < static_cast<int>(capturedStarts_.size())
        && activeSliceIndex_ < static_cast<int>(capturedEnds_.size()))
    {
        const auto idx = static_cast<size_t>(activeSliceIndex_);
        const float x0 = areaX + static_cast<float>(
            (capturedStarts_[idx] - windowStartSeconds_) / totalSeconds) * areaW;
        const float x1 = areaX + static_cast<float>(
            (capturedEnds_[idx] - windowStartSeconds_) / totalSeconds) * areaW;
        g.setColour(UiTheme::kAccentOrange.withAlpha(0.14f));
        g.fillRect(juce::Rectangle<float>(
            juce::jlimit(areaX, areaX + areaW, x0),
            areaTop,
            juce::jmax(0.0f, juce::jlimit(areaX, areaX + areaW, x1)
                                - juce::jlimit(areaX, areaX + areaW, x0)),
            areaBot - areaTop));
    }
    else if (activeSliceIndex_ >= 0
             && static_cast<size_t>(activeSliceIndex_ + 1) < edges.size())
    {
        const float x0 = areaX + edges[static_cast<size_t>(activeSliceIndex_)] * areaW;
        const float x1 = areaX + edges[static_cast<size_t>(activeSliceIndex_ + 1)] * areaW;
        g.setColour(UiTheme::kAccentOrange.withAlpha(0.14f));
        g.fillRect(juce::Rectangle<float>(x0, areaTop, x1 - x0, areaBot - areaTop));
    }

    if (preview_ != nullptr && preview_->isPlaying())
    {
        const double pos = preview_->getPositionSeconds() - windowStartSeconds_;
        if (pos >= 0.0 && pos <= totalSeconds)
        {
            const float x = areaX + static_cast<float>(pos / totalSeconds) * areaW;
            g.setColour(UiTheme::kAccentOrange);
            g.drawLine(x, areaTop, x, areaBot, 1.0f);
        }
    }
}

//==============================================================================
// Execution
//==============================================================================

bool SliceOperation::canApply() const
{
    if (!hasSample || currentPadId < 0 || audioEngine == nullptr)
        return false;
    if (type_ == Type::Transient && detectedStarts_.size() < 2)
        return false;
    if (type_ == Type::Manual
        && (capturedStarts_.size() < 2
            || capturedEnds_.size() != capturedStarts_.size()))
        return false;
    return true;
}

void SliceOperation::apply(AudioEngine& engine, int padId, juce::UndoManager& undoManager)
{
    lastApplyFailed_ = false;
    if (!canApply())
    {
        lastApplyFailed_ = true;
        return;
    }

    const double windowDuration = windowEndSeconds_ - windowStartSeconds_;
    if (windowDuration <= 0.0)
    {
        lastApplyFailed_ = true;
        return;
    }

    undoManager.beginNewTransaction("Slice Sample");
    bool ok = false;
    if (type_ == Type::Straight)
    {
        // Build N equal-division starts inside the window, in absolute file
        // seconds. endSeconds caps the last slice at windowEnd.
        std::vector<double> starts;
        starts.reserve(static_cast<size_t>(sliceCount_));
        for (int i = 0; i < sliceCount_; ++i)
            starts.push_back(windowStartSeconds_
                             + (static_cast<double>(i) / sliceCount_) * windowDuration);
        ok = undoManager.perform(new SliceSampleCommand(engine, padId,
                                                         std::move(starts),
                                                         windowEndSeconds_));
        lastAppliedSliceCount_ = sliceCount_;
    }
    else if (type_ == Type::Transient)
    {
        // detectedStarts_ already holds absolute file seconds within the
        // window. Pass a copy; the command sanitises and owns it.
        std::vector<double> starts = detectedStarts_;
        ok = undoManager.perform(new SliceSampleCommand(engine, padId,
                                                         std::move(starts),
                                                         windowEndSeconds_));
        lastAppliedSliceCount_ = static_cast<int>(detectedStarts_.size());
    }
    else  // Manual
    {
        std::vector<double> starts = capturedStarts_;
        std::vector<double> ends = capturedEnds_;
        const int appliedCount = static_cast<int>(starts.size());
        ok = undoManager.perform(new SliceSampleCommand(engine, padId,
                                                         std::move(starts),
                                                         std::move(ends),
                                                         windowEndSeconds_));
        lastAppliedSliceCount_ = appliedCount;
        if (ok)
        {
            capturedStarts_.clear();
            capturedEnds_.clear();
            clearManualHistory();
            capturing_ = false;
            activeSliceIndex_ = -1;
            if (preview_ != nullptr) preview_->stop();
        }
    }
    if (!ok)
        lastApplyFailed_ = true;
    else
    {
        auditionEndSeconds_ = 0.0;
        activeSliceIndex_ = -1;
        if (preview_ != nullptr)
            preview_->stop();
    }
}

//==============================================================================
// Helpers
//==============================================================================

int SliceOperation::snapToValidCount(double raw)
{
    const int v = static_cast<int>(std::round(raw));
    if (v <= 6)  return 4;
    if (v <= 12) return 8;
    return 16;
}

void SliceOperation::setSliceCount(int count)
{
    sliceCount_ = snapToValidCount(static_cast<double>(count));
}

void SliceOperation::ensureAnalysisUpToDate()
{
    if (type_ != Type::Transient || audioEngine == nullptr || !hasSample)
    {
        detectedStarts_.clear();
        lastAnalysedFile_ = juce::File();
        lastAnalysedSensitivity_ = -1.0;
        lastAnalysedStart_ = -1.0;
        lastAnalysedEnd_ = -1.0;
        return;
    }

    if (!currentSampleFile_.existsAsFile() || windowEndSeconds_ <= windowStartSeconds_)
    {
        detectedStarts_.clear();
        return;
    }

    // Skip if cache matches current state.
    const bool sameFile  = (lastAnalysedFile_ == currentSampleFile_);
    const bool sameSens  = std::abs(lastAnalysedSensitivity_ - sensitivity_) < 1e-6;
    const bool sameRange = std::abs(lastAnalysedStart_ - windowStartSeconds_) < 1e-6
                         && std::abs(lastAnalysedEnd_   - windowEndSeconds_)   < 1e-6;
    if (sameFile && sameSens && sameRange && !detectedStarts_.empty())
        return;

    detectedStarts_ = TransientDetector::detectSliceStarts(
        audioEngine->getEngine(), currentSampleFile_, sensitivity_,
        windowStartSeconds_, windowEndSeconds_, 16);
    lastAnalysedFile_ = currentSampleFile_;
    lastAnalysedSensitivity_ = sensitivity_;
    lastAnalysedStart_ = windowStartSeconds_;
    lastAnalysedEnd_   = windowEndSeconds_;
}

//==============================================================================
// Pad audition / Manual capture
//==============================================================================

void SliceOperation::onPadPressed(int padIdx)
{
    if (preview_ == nullptr || !hasSample)
        return;
    if (padIdx < 0 || padIdx >= 16)
        return;

    if (type_ == Type::Straight)
    {
        if (sliceCount_ <= 0 || padIdx >= sliceCount_)
            return;

        const double duration = windowEndSeconds_ - windowStartSeconds_;
        if (duration <= 0.0)
            return;

        const double start = windowStartSeconds_
            + duration * static_cast<double>(padIdx) / static_cast<double>(sliceCount_);
        auditionEndSeconds_ = windowStartSeconds_
            + duration * static_cast<double>(padIdx + 1) / static_cast<double>(sliceCount_);
        preview_->play(currentSampleFile_);
        preview_->seek(start);
        activeSliceIndex_ = padIdx;
        notifyThrottledUpdate();
        return;
    }

    if (type_ == Type::Transient)
    {
        ensureAnalysisUpToDate();
        const int count = static_cast<int>(detectedStarts_.size());
        if (padIdx >= count)
            return;

        auditionEndSeconds_ = (padIdx + 1 < count)
            ? detectedStarts_[static_cast<size_t>(padIdx + 1)]
            : windowEndSeconds_;
        preview_->play(currentSampleFile_);
        preview_->seek(detectedStarts_[static_cast<size_t>(padIdx)]);
        activeSliceIndex_ = padIdx;
        notifyThrottledUpdate();
        return;
    }

    const int N = static_cast<int>(capturedStarts_.size());
    const int K = padIdx;

    // Erase-hold: deletes capture K and reindexes. Handled in a later task;
    // placeholder for now so tests for the Erase gesture compile.
    if (eraseHeld_)
    {
        if (K < N)
        {
            beginManualEdit();
            capturedStarts_.erase(capturedStarts_.begin() + K);
            if (K < static_cast<int>(capturedEnds_.size()))
                capturedEnds_.erase(capturedEnds_.begin() + K);
            if (!overlapping_)
                makeManualRangesContiguous();
            auditionEndSeconds_ = 0.0;
            erasePadActed_ = true;
            // Keep activeSliceIndex_ pointing at the same boundary, or clear
            // it if the active boundary was the one just erased.
            if (activeSliceIndex_ == K)
                activeSliceIndex_ = -1;
            else if (activeSliceIndex_ > K)
                --activeSliceIndex_;
            notifyThrottledUpdate();
        }
        return;
    }

    if (K < N)
    {
        // During a live capture pass, already-captured pads are selection
        // targets only. Seeking here lets a duplicate press report restart
        // playback and makes the next boundary collapse against windowStart.
        // Once playback is stopped, the same pads audition normally.
        activeSliceIndex_ = K;
        auditionEndSeconds_ = K < static_cast<int>(capturedEnds_.size())
            ? capturedEnds_[static_cast<size_t>(K)]
            : windowEndSeconds_;
        if (capturing_ && preview_->isPlaying())
        {
            notifyThrottledUpdate();
            return;
        }

        preview_->seek(capturedStarts_[static_cast<size_t>(K)]);
        if (!preview_->isPlaying())
        {
            preview_->play(currentSampleFile_);
            preview_->seek(capturedStarts_[static_cast<size_t>(K)]);
        }
        notifyThrottledUpdate();
        return;
    }

    if (K == N && N == 0)
    {
        beginManualEdit();
        preview_->play(currentSampleFile_);
        preview_->seek(windowStartSeconds_);
        capturedStarts_.push_back(windowStartSeconds_);
        capturedEnds_.push_back(windowEndSeconds_);
        capturing_ = true;
        activeSliceIndex_ = 0;
        auditionEndSeconds_ = windowEndSeconds_;
        notifyThrottledUpdate();
        return;
    }

    if (K == N && N > 0 && N < 16)
    {
        double t = preview_->getPositionSeconds();
        const double floorT = capturedStarts_.back() + kMinSliceSeconds_;
        t = juce::jlimit(floorT, windowEndSeconds_, t);
        beginManualEdit();
        if (!capturedEnds_.empty())
            capturedEnds_.back() = t;
        capturedStarts_.push_back(t);
        capturedEnds_.push_back(windowEndSeconds_);
        activeSliceIndex_ = static_cast<int>(capturedStarts_.size()) - 1;
        auditionEndSeconds_ = windowEndSeconds_;
        notifyThrottledUpdate();
        return;
    }

    // K > N or N == 16: ignored.
}

void SliceOperation::onEraseEvent(bool pressed)
{
    if (type_ != Type::Manual)
        return;

    if (pressed)
    {
        eraseHeld_ = true;
        erasePadActed_ = false;
        notifyThrottledUpdate();
        return;
    }

    // Release
    const bool shouldFireEraseAll = !erasePadActed_;
    eraseHeld_ = false;
    erasePadActed_ = false;
    notifyThrottledUpdate();

    if (shouldFireEraseAll && onEraseAll_)
        onEraseAll_();
}

void SliceOperation::confirmEraseAll()
{
    if (preview_ != nullptr)
        preview_->stop();
    beginManualEdit();
    capturedStarts_.clear();
    capturedEnds_.clear();
    capturing_ = false;
    activeSliceIndex_ = -1;
    auditionEndSeconds_ = 0.0;
    notifyThrottledUpdate();
}

void SliceOperation::nudgeActiveSlice(double deltaSeconds)
{
    if (type_ != Type::Manual) return;
    if (activeSliceIndex_ < 0) return;
    const int N = static_cast<int>(capturedStarts_.size());
    if (activeSliceIndex_ >= N) return;

    const double lo = overlapping_ || activeSliceIndex_ == 0
        ? windowStartSeconds_
        : capturedStarts_[static_cast<size_t>(activeSliceIndex_ - 1)]
            + kMinSliceSeconds_;
    const double hi = capturedEnds_[static_cast<size_t>(activeSliceIndex_)]
        - kMinSliceSeconds_;

    double newValue = capturedStarts_[static_cast<size_t>(activeSliceIndex_)] + deltaSeconds;
    newValue = juce::jlimit(lo, hi, newValue);
    if (std::abs(newValue - capturedStarts_[static_cast<size_t>(activeSliceIndex_)]) < 1e-9)
        return;
    beginManualEdit();
    capturedStarts_[static_cast<size_t>(activeSliceIndex_)] = newValue;
    if (!overlapping_ && activeSliceIndex_ > 0)
        capturedEnds_[static_cast<size_t>(activeSliceIndex_ - 1)] = newValue;
    notifyThrottledUpdate();
}

void SliceOperation::beginManualEdit()
{
    manualUndoStack_.push_back({
        capturedStarts_, capturedEnds_, overlapping_, activeSliceIndex_
    });
    manualRedoStack_.clear();
}

void SliceOperation::restoreManualState()
{
    capturing_ = !capturedStarts_.empty();
    if (capturedStarts_.empty())
        activeSliceIndex_ = -1;
    else
        activeSliceIndex_ = juce::jlimit(
            0, static_cast<int>(capturedStarts_.size()) - 1,
            activeSliceIndex_);
    auditionEndSeconds_ = activeSliceIndex_ >= 0
        && activeSliceIndex_ < static_cast<int>(capturedEnds_.size())
            ? capturedEnds_[static_cast<size_t>(activeSliceIndex_)]
            : 0.0;
    if (capturedStarts_.empty() && preview_ != nullptr)
        preview_->stop();
    notifyThrottledUpdate();
}

void SliceOperation::makeManualRangesContiguous()
{
    capturedEnds_.resize(capturedStarts_.size(), windowEndSeconds_);
    for (size_t i = 0; i + 1 < capturedStarts_.size(); ++i)
        capturedEnds_[i] = capturedStarts_[i + 1];
    if (!capturedEnds_.empty())
        capturedEnds_.back() = windowEndSeconds_;
}

void SliceOperation::clearManualHistory()
{
    manualUndoStack_.clear();
    manualRedoStack_.clear();
}

bool SliceOperation::undoManualEdit()
{
    if (type_ != Type::Manual || manualUndoStack_.empty())
        return false;

    manualRedoStack_.push_back({
        capturedStarts_, capturedEnds_, overlapping_, activeSliceIndex_
    });
    auto previous = std::move(manualUndoStack_.back());
    manualUndoStack_.pop_back();
    capturedStarts_ = std::move(previous.starts);
    capturedEnds_ = std::move(previous.ends);
    overlapping_ = previous.overlapping;
    activeSliceIndex_ = previous.activeSliceIndex;
    restoreManualState();
    return true;
}

bool SliceOperation::redoManualEdit()
{
    if (type_ != Type::Manual || manualRedoStack_.empty())
        return false;

    manualUndoStack_.push_back({
        capturedStarts_, capturedEnds_, overlapping_, activeSliceIndex_
    });
    auto next = std::move(manualRedoStack_.back());
    manualRedoStack_.pop_back();
    capturedStarts_ = std::move(next.starts);
    capturedEnds_ = std::move(next.ends);
    overlapping_ = next.overlapping;
    activeSliceIndex_ = next.activeSliceIndex;
    restoreManualState();
    return true;
}

std::vector<SliceOperation::ManualSliceRange>
SliceOperation::getManualSliceRanges() const
{
    std::vector<ManualSliceRange> ranges;
    const size_t count = juce::jmin(capturedStarts_.size(), capturedEnds_.size());
    ranges.reserve(count);
    for (size_t i = 0; i < count; ++i)
        ranges.push_back({ capturedStarts_[i], capturedEnds_[i] });
    return ranges;
}

void SliceOperation::setOverlapping(bool overlapping)
{
    if (type_ != Type::Manual || overlapping_ == overlapping)
        return;

    beginManualEdit();
    overlapping_ = overlapping;
    if (!overlapping_)
        makeManualRangesContiguous();
    restoreManualState();
}

bool SliceOperation::selectManualSlice(int index)
{
    if (type_ != Type::Manual
        || !juce::isPositiveAndBelow(index, static_cast<int>(capturedStarts_.size())))
        return false;

    activeSliceIndex_ = index;
    auditionEndSeconds_ = index < static_cast<int>(capturedEnds_.size())
        ? capturedEnds_[static_cast<size_t>(index)]
        : windowEndSeconds_;
    notifyThrottledUpdate();
    return true;
}

bool SliceOperation::selectAdjacentManualSlice(int direction)
{
    if (type_ != Type::Manual || capturedStarts_.empty() || direction == 0)
        return false;

    const int count = static_cast<int>(capturedStarts_.size());
    const int current = activeSliceIndex_ >= 0 ? activeSliceIndex_ : 0;
    const int next = juce::jlimit(0, count - 1, current + (direction > 0 ? 1 : -1));
    if (next == current)
        return false;
    return selectManualSlice(next);
}
