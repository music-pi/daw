#include "PadDetailsWidget.h"

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/GroupManager.h"
#include "../theme/UiTheme.h"
#include "WindowManager.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// Choke group UI: index 0 = None (storage 0), index 1 = Self (storage -1),
// indices 2..9 = Group 1..8 (storage 1..8).
const std::array<juce::String, 10> kChokeGroupLabels{
    "None",
    "Self",
    "Group 1",
    "Group 2",
    "Group 3",
    "Group 4",
    "Group 5",
    "Group 6",
    "Group 7",
    "Group 8"
};
const std::array<uint8_t, SamplerInstrument::kMaxSampleLayers>
    kLayerPadColours {
        HardwareConstants::kIndexedColorPairs[6].dim,
        HardwareConstants::kIndexedColorPairs[10].dim,
        HardwareConstants::kIndexedColorPairs[11].dim,
        HardwareConstants::kIndexedColorPairs[13].dim
    };
} // namespace

PadDetailsWidget::PadDetailsWidget()
{
    thumbnailFormatManager_.registerBasicFormats();
    // The field is a T9 driver, not a rendered component. Keeping it as a
    // child but invisible hands findEnclosingWidget() a well-defined parent.
    addChildComponent(renameField_);
}

PadDetailsWidget::~PadDetailsWidget() = default;

// -- Widget identity --

WidgetDescriptor PadDetailsWidget::describe() const
{
    return { "pad_details", 2, false, DisplayConstraint::Any };
}

juce::String PadDetailsWidget::getTitle() const
{
    // Live-paint the in-flight T9 buffer into the titlebar with a trailing
    // block-cursor so the user sees what they're typing, same as PatternWidget.
    if (renameField_.isEditing())
        return renameField_.getText()
             + juce::String::charToString((juce::juce_wchar) 0x2588);
    const auto title = padTitle_.isNotEmpty() ? padTitle_ : juce::String("Pad Details");
    return currentPage() == 1 ? title + " · Layer Mix" : title;
}

juce::Colour PadDetailsWidget::getAccentColour() const
{
    // Note: we can't call engine() here (asserts non-null). Read indirectly
    // via a const method or skip if not yet attached. PadDetailsWidget stores
    // the active group colour in cachedAccent_ each refresh.
    return cachedAccent_;
}

// -- Lifecycle --

void PadDetailsWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    currentPage_ = 0;
    tickDiv_ = 0;
    refreshSelection();
    updatePageArrowLeds();

    if (auto* host = controllerHost(); host != nullptr)
    {
        if (auto* input = host->getInputManager(); input != nullptr)
        {
            layerPadBinding_ = ScopedInputHandler(
                input,
                input->addPadHandler(
                    InputManager::HandlerPriority::Modal, "",
                    [this](InputEvent& event) { handleLayerPadInput(event); }));
        }
    }
}

void PadDetailsWidget::onDeactivated()
{
    layerPadBinding_ = {};
    setLayerPadMode(false);
    hw().setLed("arrowLeft", 0, "pad_details");
    hw().setLed("arrowRight", 0, "pad_details");
    // Clear callbacks before ending edit so nothing fires during teardown —
    // cancel would pop the stashed widget back into a panel we no longer own.
    renameField_.setOnCommit(nullptr);
    renameField_.setOnCancel(nullptr);
    renameField_.setOnTextChanged(nullptr);
    if (renameField_.isEditing())
        renameField_.endEdit(false);
    renameStashedRight_.reset();
}

void PadDetailsWidget::onPageVisible(int page)
{
    currentPage_ = juce::jlimit(0, describe().pageCount - 1, page);
    updatePageArrowLeds();
    repaint();
}

// -- Resources --

std::vector<std::string> PadDetailsWidget::requiredResources(int page)
{
    if (page < 0 || page > 1)
        return {};

    std::vector<std::string> resources;
    resources.push_back("arrowLeft");
    resources.push_back("arrowRight");
    if (layerPadMode_)
        for (int i = 1; i <= HardwareConstants::kPadCount; ++i)
            resources.push_back("p" + std::to_string(i));

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

    return resources;
}

// -- Options & Knobs --

std::vector<Option> PadDetailsWidget::getOptions(int page)
{
    if (page == 1)
    {
        Option back;
        back.id = "pad.layers.back";
        back.label = "Pad";
        back.state = OptionState::Enabled;
        back.onInvoke = [this]() {
            if (auto* manager = windowManager()) manager->scrollLeft();
        };
        Option empty;
        empty.state = OptionState::Empty;
        return { back, empty, empty, empty };
    }
    if (page != 0)
        return {};

    std::vector<Option> options(4);

    // Shift swaps d1 from "Load Sample" to "Rename" — same UX as PatternWidget.
    const bool shift = controllerHost() != nullptr && controllerHost()->isShiftPressed();

    if (shift)
    {
        Option rename;
        rename.id = "pad.rename";
        rename.label = "Rename";
        rename.state = (hasSelection_ && currentPadId_ >= 0)
                           ? OptionState::Enabled
                           : OptionState::Disabled;
        rename.onInvoke = [this]() { beginPadRename(); };
        options[0] = std::move(rename);
    }
    else
    {
        Option loadSample;
        loadSample.id = "loadSample";
        loadSample.label = "Load Sample";
        loadSample.state = hasSelection_ ? OptionState::Enabled : OptionState::Disabled;
        loadSample.onInvoke = [this]()
        {
            if (browseCallback_ && currentPadId_ >= 0)
                browseCallback_(currentPadId_);
        };
        options[0] = std::move(loadSample);
    }

    Option layer;
    layer.id = shift ? "pad.removeLayer" : "pad.addLayer";
    layer.label = shift ? "Remove Layer" : "Add Layer";
    layer.state = hasSelection_ && currentPad_.hasSample
        && (shift ? currentPad_.sampleLayerCount > 1
                  : currentPad_.sampleLayerCount < SamplerInstrument::kMaxSampleLayers)
        ? OptionState::Enabled : OptionState::Disabled;
    layer.onInvoke = [this, shift]()
    {
        if (currentPadId_ < 0)
            return;
        if (shift)
        {
            engine().getSampler().removeLastSampleLayer(currentPadId_);
            refreshSelection();
        }
        else if (browseLayerCallback_)
        {
            browseLayerCallback_(currentPadId_);
        }
    };
    options[1] = std::move(layer);

    Option triggerMode;
    triggerMode.id = "pad.triggerMode";
    triggerMode.label = SamplerInstrument::triggerModeName(currentPad_.triggerMode);
    triggerMode.state = hasSelection_ && currentPad_.hasSample
        ? OptionState::Enabled : OptionState::Disabled;
    triggerMode.onInvoke = [this]() { cycleTriggerMode(); };
    options[2] = std::move(triggerMode);

    // Slot 4: layer audition for multi-sample pads. Clear Pad remains
    // available under Shift so audition doesn't remove an existing action.
    if (hasSelection_ && currentPad_.hasSample)
    {
        Option action;
        if (shift || currentPad_.sampleLayerCount <= 1)
        {
            action.id = "clearPad";
            action.label = "Clear Pad";
            action.state = OptionState::Enabled;
            action.onInvoke = [this]()
            {
                if (currentPadId_ >= 0)
                {
                    setLayerPadMode(false);
                    engine().getSampler().clearSample(currentPadId_);
                    refreshSelection();
                }
            };
        }
        else
        {
            action.id = "pad.layerToPads";
            action.label = "Layer > Pads";
            action.state = layerPadMode_ ? OptionState::Active
                                         : OptionState::Enabled;
            action.onInvoke = [this]() { setLayerPadMode(! layerPadMode_); };
        }
        options[3] = std::move(action);
    }
    else
    {
        options[3].state = OptionState::Empty;
    }

    return options;
}

std::vector<Knob> PadDetailsWidget::getKnobs(int page)
{
    if (page == 1)
    {
        std::vector<Knob> knobs(4);
        const int count = static_cast<int>(currentPad_.sampleLayers.size());
        const bool singleLayer = count == 1;
        selectedLayerIndex_ = count > 0
            ? juce::jlimit(0, count - 1, selectedLayerIndex_) : 0;

        const auto* selected = count > 0
            ? &currentPad_.sampleLayers[static_cast<size_t>(selectedLayerIndex_)]
            : nullptr;

        Knob first;
        first.isEnabled = selected != nullptr;
        if (singleLayer)
        {
            first.id = "pad.layer.curve";
            first.label = "Velocity";
            Knob::ListModel curveModel;
            curveModel.entries = { "Linear", "Soft", "Hard", "Fixed" };
            curveModel.selectedIndex = selected != nullptr
                ? static_cast<int>(selected->velocityCurve) : 0;
            curveModel.onChange = [this](int index) {
                setLayerVelocityCurve(index);
            };
            first.model = std::move(curveModel);
        }
        else
        {
            first.id = "pad.layer.select";
            first.label = "Layer";
            Knob::ListModel layerModel;
            for (int i = 0; i < count; ++i)
                layerModel.entries.push_back(
                    juce::String(i + 1) + " "
                    + currentPad_.sampleLayers[static_cast<size_t>(i)].name);
            layerModel.selectedIndex = selectedLayerIndex_;
            layerModel.onChange = [this](int index) {
                selectedLayerIndex_ = index;
                repaint();
            };
            first.model = std::move(layerModel);
        }
        knobs[0] = std::move(first);

        Knob gain;
        gain.id = "pad.layer.gain";
        gain.label = "Gain";
        gain.isEnabled = selected != nullptr;
        gain.continuousMode = true;
        gain.model = Knob::NumericModel {
            .value = selected != nullptr ? selected->gainDb : 0.0,
            .minimum = -48.0,
            .maximum = 24.0,
            .step = 0.5,
            .onChange = [this](double value) { setLayerGain(value); },
            .formatter = [](double value) { return juce::String(value, 1) + " dB"; }
        };
        knobs[1] = std::move(gain);

        if (singleLayer)
        {
            Knob minimum;
            minimum.id = "pad.layer.velocityMinimum";
            minimum.label = "Vel Min";
            minimum.isEnabled = selected != nullptr;
            minimum.continuousMode = true;
            minimum.model = Knob::NumericModel {
                .value = selected != nullptr
                    ? selected->velocityMinimum * 100.0 : 85.0,
                .minimum = 1.0,
                .maximum = selected != nullptr
                    ? selected->velocityMaximum * 100.0 : 100.0,
                .step = 1.0,
                .onChange = [this](double value) {
                    setLayerVelocityMinimum(value);
                },
                .formatter = [](double value) {
                    return juce::String(juce::roundToInt(value)) + "%";
                }
            };
            knobs[2] = std::move(minimum);

            Knob maximum;
            maximum.id = "pad.layer.velocityMaximum";
            maximum.label = "Vel Max";
            maximum.isEnabled = selected != nullptr;
            maximum.continuousMode = true;
            maximum.model = Knob::NumericModel {
                .value = selected != nullptr
                    ? selected->velocityMaximum * 100.0 : 100.0,
                .minimum = selected != nullptr
                    ? selected->velocityMinimum * 100.0 : 1.0,
                .maximum = 100.0,
                .step = 1.0,
                .onChange = [this](double value) {
                    setLayerVelocityMaximum(value);
                },
                .formatter = [](double value) {
                    return juce::String(juce::roundToInt(value)) + "%";
                }
            };
            knobs[3] = std::move(maximum);
        }
        else
        {
            Knob weight;
            weight.id = "pad.layer.weight";
            weight.label = "Random Wt";
            weight.isEnabled = selected != nullptr;
            weight.continuousMode = true;
            weight.model = Knob::NumericModel {
                .value = selected != nullptr
                    ? selected->randomWeight * 100.0 : 0.0,
                .minimum = 0.0,
                .maximum = 100.0,
                .step = 5.0,
                .onChange = [this](double value) { setLayerWeight(value); },
                .formatter = [](double value) {
                    return juce::String(juce::roundToInt(value)) + "%";
                }
            };
            knobs[2] = std::move(weight);

            Knob curve;
            curve.id = "pad.layer.curve";
            curve.label = "Velocity";
            curve.isEnabled = selected != nullptr;
            Knob::ListModel curveModel;
            curveModel.entries = { "Linear", "Soft", "Hard", "Fixed" };
            curveModel.selectedIndex = selected != nullptr
                ? static_cast<int>(selected->velocityCurve) : 0;
            curveModel.onChange = [this](int index) {
                setLayerVelocityCurve(index);
            };
            curve.model = std::move(curveModel);
            knobs[3] = std::move(curve);
        }

        currentKnobs_ = knobs;
        return knobs;
    }
    if (page != 0)
        return {};

    std::vector<Knob> knobSlots(4);
    const bool sampleLoaded = hasSelection_ && currentPad_.hasSample && currentPad_.totalLengthSeconds > 0.0;
    const double totalLen = sampleLoaded ? currentPad_.totalLengthSeconds : 1.0;

    auto formatSeconds = [](double s) {
        if (s < 1.0)
            return juce::String(s * 1000.0, 0) + " ms";
        return juce::String(s, 2) + " s";
    };

    // Slot 1: Start (sample window start, seconds)
    Knob startKnob;
    startKnob.id = "padStart";
    startKnob.label = "Start";
    startKnob.isEnabled = sampleLoaded;
    startKnob.continuousMode = true;

    Knob::NumericModel startModel;
    startModel.minimum = 0.0;
    startModel.maximum = totalLen;
    startModel.step = 0.002;  // 2 ms fixed; shift -> 0.1 ms ultra-fine
    startModel.value = sampleLoaded ? currentPad_.windowStartSeconds : 0.0;
    startModel.formatter = formatSeconds;
    startModel.onChange = [this](double startSec) {
        onRangeChanged(startSec, currentPad_.windowEndSeconds);
    };
    startKnob.model = std::move(startModel);
    knobSlots[kStartKnobIndex] = std::move(startKnob);

    // Slot 2: End (sample window end, seconds)
    Knob endKnob;
    endKnob.id = "padEnd";
    endKnob.label = "End";
    endKnob.isEnabled = sampleLoaded;
    endKnob.continuousMode = true;

    Knob::NumericModel endModel;
    endModel.minimum = 0.0;
    endModel.maximum = totalLen;
    endModel.step = 0.002;  // 2 ms fixed; shift -> 0.1 ms ultra-fine
    endModel.value = sampleLoaded ? currentPad_.windowEndSeconds : 0.0;
    endModel.formatter = formatSeconds;
    endModel.onChange = [this](double endSec) {
        onRangeChanged(currentPad_.windowStartSeconds, endSec);
    };
    endKnob.model = std::move(endModel);
    knobSlots[kEndKnobIndex] = std::move(endKnob);

    // Slot 3: Gain (sample normalization gain — reflected in waveform viz)
    Knob gainKnob;
    gainKnob.id = "padGain";
    gainKnob.label = "Gain";
    gainKnob.isEnabled = sampleLoaded;
    gainKnob.continuousMode = true;

    Knob::NumericModel gainModel;
    gainModel.minimum = -24.0;
    gainModel.maximum = 24.0;
    gainModel.step = 0.5;
    gainModel.value = sampleGainDb_;
    gainModel.formatter = [](double db) { return juce::String(db, 1) + " dB"; };
    gainModel.onChange = [this](double db) { onGainChanged(db); };
    gainKnob.model = std::move(gainModel);
    knobSlots[kGainKnobIndex] = std::move(gainKnob);

    // Slot 4: Choke group
    Knob chokeKnob;
    chokeKnob.id = "padChokeGroup";
    chokeKnob.label = "Choke";
    chokeKnob.isEnabled = hasSelection_;

    Knob::ListModel listModel;
    listModel.entries.reserve(kChokeGroupLabels.size());
    for (const auto& label : kChokeGroupLabels)
        listModel.entries.push_back(label);
    listModel.selectedIndex = hasSelection_ ? chokeIndexFromStorage(currentPad_.chokeGroup) : 0;
    listModel.onChange = [this](int index)
    {
        onChokeGroupChanged(index);
    };

    chokeKnob.model = std::move(listModel);
    knobSlots[kChokeKnobIndex] = std::move(chokeKnob);

    currentKnobs_ = knobSlots;
    return knobSlots;
}

void PadDetailsWidget::handleKnob(int localIndex, int16_t delta, uint16_t /*absolute*/, bool shift)
{
    if (localIndex < 0 || localIndex >= static_cast<int>(currentKnobs_.size()))
        return;

    auto& knob = currentKnobs_[static_cast<size_t>(localIndex)];
    if (!knob.isEnabled)
        return;

    // Accumulate raw hardware deltas and emit a discrete step only after
    // crossing kKnobThreshold — prevents runaway speed on sensitive encoders.
    auto& acc = knobAccumulator_[static_cast<size_t>(localIndex)];
    acc += delta;
    if (std::abs(acc) < kKnobThreshold)
        return;

    const int direction = acc > 0 ? 1 : -1;
    acc = 0;

    if (auto* numModel = std::get_if<Knob::NumericModel>(&knob.model))
    {
        const double mult = shift ? 0.05 : 1.0;
        double newValue = numModel->value + direction * numModel->step * mult;
        newValue = juce::jlimit(numModel->minimum, numModel->maximum, newValue);
        if (newValue == numModel->value)
            return;
        numModel->value = newValue;
        if (numModel->onChange)
            numModel->onChange(newValue);
    }
    else if (auto* listModel = std::get_if<Knob::ListModel>(&knob.model))
    {
        if (listModel->entries.empty())
            return;
        int newIndex = listModel->selectedIndex + direction;
        newIndex = juce::jlimit(0, static_cast<int>(listModel->entries.size()) - 1, newIndex);
        if (newIndex == listModel->selectedIndex)
            return;
        listModel->selectedIndex = newIndex;
        if (listModel->onChange)
            listModel->onChange(newIndex);
    }
}

void PadDetailsWidget::onRangeChanged(double startSeconds, double endSeconds)
{
    if (!hasSelection_ || currentPadId_ < 0 || !currentPad_.hasSample)
        return;

    constexpr double kMinWindowSeconds = 0.001;
    const double total = currentPad_.totalLengthSeconds;

    // Clamp both ends into the sample and guarantee start + minWindow <= end.
    // If start is pushed up against the sample end, pull it back so end can fit.
    startSeconds = juce::jlimit(0.0, juce::jmax(0.0, total - kMinWindowSeconds), startSeconds);
    endSeconds = juce::jlimit(startSeconds + kMinWindowSeconds, total, endSeconds);

    engine().getSampler().setPadSampleRange(currentPadId_, startSeconds, endSeconds);
    currentPad_.windowStartSeconds = startSeconds;
    currentPad_.windowEndSeconds = endSeconds;
}

// -- Rendering --

void PadDetailsWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void PadDetailsWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (page == 1)
    {
        g.fillAll(UiTheme::kBackgroundDark);
        auto content = bounds.reduced(
            UiTheme::kHorizontalPadding, UiTheme::kVerticalPadding);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody,
                                               juce::Font::bold)));
        g.drawText("Round-robin layers", content.removeFromTop(24),
                   juce::Justification::centredLeft);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall)));
        for (size_t i = 0; i < currentPad_.sampleLayers.size(); ++i)
        {
            const auto& layer = currentPad_.sampleLayers[i];
            auto row = content.removeFromTop(24);
            g.setColour(static_cast<int>(i) == selectedLayerIndex_
                            ? UiTheme::kTitlebarAccent
                            : UiTheme::kTextSecondary);
            const auto text = juce::String(static_cast<int>(i + 1)) + "  "
                + layer.name + "   " + juce::String(layer.gainDb, 1) + " dB   "
                + juce::String(juce::roundToInt(layer.randomWeight * 100.0f)) + "%   "
                + SamplerInstrument::velocityCurveName(layer.velocityCurve);
            g.drawFittedText(text, row, juce::Justification::centredLeft, 1);
        }
        if (currentPad_.sampleLayers.empty())
        {
            g.setColour(UiTheme::kTextSecondary);
            g.drawFittedText("Load a sample, then use Add Layer.", content,
                             juce::Justification::centred, 2);
        }
        return;
    }
    if (page != 0)
        return;

    auto content = bounds.reduced(UiTheme::kHorizontalPadding, UiTheme::kVerticalPadding);

    if (!hasSelection_)
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
        g.drawFittedText("Select a pad from the overview grid to inspect its details.",
                         content, juce::Justification::centred, 3);
        return;
    }

    // Waveform display — left half of content
    auto waveArea = content.removeFromLeft(content.getWidth() / 2).reduced(2);

    g.setColour(UiTheme::kOptionFillEmpty);
    g.fillRect(waveArea);
    g.setColour(UiTheme::kOptionBorderEmpty);
    g.drawRect(waveArea, 1);

    if (currentPad_.hasSample && thumbnail_ != nullptr && thumbnail_->getTotalLength() > 0.0)
    {
        auto thumbArea = waveArea.reduced(4);
        const double total = thumbnail_->getTotalLength();

        // Zoom into the pad's active region. The pad represents a slice of a
        // potentially larger parent sample — show a little context margin on
        // either side so it's visually obvious that more audio exists outside
        // the current window. Margin area is shaded.
        const double winStart = juce::jlimit(0.0, total, currentPad_.windowStartSeconds);
        const double winEnd   = juce::jlimit(winStart, total, currentPad_.windowEndSeconds);
        const double winDur   = juce::jmax(winEnd - winStart, 0.001);

        const double margin      = juce::jmin(winDur * 0.10, total * 0.05);
        const double visibleStart = juce::jmax(0.0,   winStart - margin);
        const double visibleEnd   = juce::jmin(total, winEnd   + margin);
        const double visibleSpan  = juce::jmax(visibleEnd - visibleStart, 0.001);

        g.setColour(juce::Colours::white.withAlpha(0.7f));
        const float verticalZoom = juce::Decibels::decibelsToGain(sampleGainDb_);
        thumbnail_->drawChannels(g, thumbArea, visibleStart, visibleEnd, verticalZoom);

        // Start/end markers + margin shading, relative to the zoomed view.
        if (currentPad_.totalLengthSeconds > 0.0)
        {
            const float areaX = static_cast<float>(thumbArea.getX());
            const float areaW = static_cast<float>(thumbArea.getWidth());

            const float startX = areaX + static_cast<float>((winStart - visibleStart) / visibleSpan) * areaW;
            const float endX   = areaX + static_cast<float>((winEnd   - visibleStart) / visibleSpan) * areaW;

            // Dim the margin regions — visual cue that the pad is a slice of a
            // larger source file.
            g.setColour(juce::Colour(0x00, 0x00, 0x00).withAlpha(0.55f));
            if (startX > areaX)
                g.fillRect(juce::Rectangle<float>(areaX, static_cast<float>(thumbArea.getY()),
                                                  startX - areaX, static_cast<float>(thumbArea.getHeight())));
            if (endX < areaX + areaW)
                g.fillRect(juce::Rectangle<float>(endX, static_cast<float>(thumbArea.getY()),
                                                  (areaX + areaW) - endX, static_cast<float>(thumbArea.getHeight())));

            // Start/end marker lines.
            g.setColour(UiTheme::kAccentGreen);
            g.fillRect(juce::Rectangle<float>(startX, static_cast<float>(thumbArea.getY()),
                                              1.0f, static_cast<float>(thumbArea.getHeight())));
            g.setColour(UiTheme::kAccentRed);
            g.fillRect(juce::Rectangle<float>(endX - 1.0f, static_cast<float>(thumbArea.getY()),
                                              1.0f, static_cast<float>(thumbArea.getHeight())));
        }
    }
    else
    {
        g.setColour(UiTheme::kTextSecondary);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall)));
        g.drawFittedText("No sample loaded", waveArea, juce::Justification::centred, 1);
    }

    // Info column — right half
    auto info = content.reduced(UiTheme::kPadGridGap, 0);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody, juce::Font::bold)));
    g.drawText(sampleStatus_, info.removeFromTop(20), juce::Justification::centredLeft);

    g.setColour(UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall)));
    g.drawFittedText(samplePath_, info.removeFromTop(32), juce::Justification::topLeft, 2);

    info.removeFromTop(4);

    if (instrumentName_.isNotEmpty())
    {
        g.setColour(UiTheme::kAccentGreen);
        g.drawText("Instr: " + instrumentName_, info.removeFromTop(18),
                   juce::Justification::centredLeft);
    }

    g.setColour(UiTheme::kTextSecondary);
    g.drawText(midiInfo_, info.removeFromTop(18), juce::Justification::centredLeft);
}

// -- Input --

void PadDetailsWidget::handlePad(const controller_events::PadEvent& e)
{
    if (! layerPadMode_ || currentPadId_ < 0)
        return;

    const int layerIndex = static_cast<int>(e.pad);
    const int count = static_cast<int>(currentPad_.sampleLayers.size());
    if (! juce::isPositiveAndBelow(layerIndex, count))
        return;

    auto& held = layerPadsHeld_[static_cast<size_t>(layerIndex)];
    if (e.pressed)
    {
        // MK3 pressure updates while a finger is down may arrive with the
        // pressed bit still set. Only the actual up->down transition auditions.
        if (held)
            return;
        held = true;
        const float normalized = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(e.pressure)
                / static_cast<float>(HardwareConstants::kPadPressureMax));
        const float velocity = normalized > 0.0f
            ? juce::jlimit(0.15f, 1.0f, std::sqrt(normalized)) : 1.0f;
        engine().getSampler().triggerSampleLayer(
            currentPadId_, layerIndex, velocity);
    }
    else
    {
        if (! held)
            return;
        held = false;
        engine().getSampler().releaseSampleLayer(currentPadId_, layerIndex);
    }
    refreshPadLeds();
}

void PadDetailsWidget::refreshPadLeds()
{
    if (! layerPadMode_ || ! hasHardware())
        return;

    const auto owner = describe().id.toStdString();
    const int count = juce::jmin(
        SamplerInstrument::kMaxSampleLayers,
        static_cast<int>(currentPad_.sampleLayers.size()));
    for (int i = 0; i < HardwareConstants::kPadCount; ++i)
    {
        uint8_t colour = HardwareConstants::kColorOff;
        if (i < count)
        {
            colour = kLayerPadColours[static_cast<size_t>(i)];
            if (layerPadsHeld_[static_cast<size_t>(i)])
                colour = HardwareConstants::brightestIndexedVariant(colour);
        }
        hw().setLed("p" + std::to_string(i + 1), colour, owner);
    }
}

void PadDetailsWidget::handleButton(const controller_events::ButtonEvent& e)
{
    if (!e.pressed)
        return;

    if (e.name == "back" || e.name == "escape")
    {
        if (backCallback_)
            backCallback_();
        return;
    }

    if (windowManager() == nullptr)
        return;
    if (e.name == "arrowLeft")
        windowManager()->scrollLeft();
    else if (e.name == "arrowRight")
        windowManager()->scrollRight();
}

void PadDetailsWidget::setBackCallback(std::function<void()> callback)
{
    backCallback_ = std::move(callback);
}

void PadDetailsWidget::setBrowseCallback(std::function<void(int padId)> callback)
{
    browseCallback_ = std::move(callback);
}

void PadDetailsWidget::setBrowseLayerCallback(
    std::function<void(int padId)> callback)
{
    browseLayerCallback_ = std::move(callback);
}

void PadDetailsWidget::triggerSetInstrument()
{
    if (!hasSelection_ || currentPadId_ < 0)
        return;
    if (onSetInstrument_)
        onSetInstrument_(currentPadId_);
}

void PadDetailsWidget::triggerClearInstrument()
{
    if (!hasSelection_ || currentPadId_ < 0)
        return;
    if (onClearInstrument_)
        onClearInstrument_(currentPadId_);
}

// -- Private --

void PadDetailsWidget::onUiHostTick()
{
    if (++tickDiv_ < 6) return;
    tickDiv_ = 0;
    refreshSelection();
}

void PadDetailsWidget::updatePageArrowLeds()
{
    if (! hasHardware())
        return;
    hw().setLed("arrowLeft", currentPage_ > 0
        ? HardwareConstants::kLedDim : 0, "pad_details");
    hw().setLed("arrowRight", currentPage_ < describe().pageCount - 1
        ? HardwareConstants::kLedDim : 0, "pad_details");
}

void PadDetailsWidget::setLayerPadMode(bool enabled)
{
    enabled = enabled && hasSelection_ && currentPadId_ >= 0
        && currentPad_.sampleLayerCount > 1;
    if (layerPadMode_ == enabled)
        return;

    const auto owner = describe().id.toStdString();
    if (enabled)
    {
        layerPadMode_ = true;
        layerPadsHeld_.fill(false);
        for (int i = 0; i < HardwareConstants::kPadCount; ++i)
        {
            const auto resource = "p" + std::to_string(i + 1);
            savedPadOwners_[static_cast<size_t>(i)] = hw().getOwner(resource);
            if (hw().isClaimed(resource) && hw().getOwner(resource) != owner)
                hw().forceRelease(resource, false);
            hw().claim(resource, owner);
        }
        refreshPadLeds();
    }
    else
    {
        if (currentPadId_ >= 0)
            engine().getSampler().stopPad(currentPadId_);
        layerPadMode_ = false;
        layerPadsHeld_.fill(false);
        restoreLayerPadOwners();
    }
    repaint();
}

void PadDetailsWidget::handleLayerPadInput(InputEvent& event)
{
    if (! layerPadMode_)
        return;

    const int pad = event.padIndex();
    if (pad < 0 || pad >= HardwareConstants::kPadCount)
        return;

    controller_events::PadEvent translated;
    translated.pad = static_cast<uint8_t>(pad);
    translated.pressed = event.isPressed();
    translated.pressure = event.metadata.contains("pressure")
        ? static_cast<uint16_t>(static_cast<int>(event.metadata["pressure"]))
        : 0;
    handlePad(translated);
    event.consumed = true;
}

void PadDetailsWidget::restoreLayerPadOwners()
{
    const auto owner = describe().id.toStdString();
    for (int i = 0; i < HardwareConstants::kPadCount; ++i)
    {
        const auto resource = "p" + std::to_string(i + 1);
        if (hw().getOwner(resource) == owner)
            hw().release(resource, owner);

        const auto& savedOwner = savedPadOwners_[static_cast<size_t>(i)];
        if (! savedOwner.empty() && ! hw().isClaimed(resource))
            hw().claim(resource, savedOwner);
        savedPadOwners_[static_cast<size_t>(i)].clear();
    }

    if (auto* manager = windowManager(); manager != nullptr)
    {
        for (const auto side : { DisplaySide::Left, DisplaySide::Right })
            if (auto* widget = manager->getWidget(side); widget != nullptr
                && widget != this)
                widget->refreshPadLeds();
    }
}

int PadDetailsWidget::chokeStorageFromIndex(int index) const
{
    if (index <= 0) return 0;        // None
    if (index == 1) return -1;       // Self
    return index - 1;                // Group 1..8 → 1..8
}

int PadDetailsWidget::chokeIndexFromStorage(int storage) const
{
    if (storage == 0) return 0;      // None
    if (storage == -1) return 1;     // Self
    if (storage >= 1 && storage <= 8) return storage + 1; // 1..8 → 2..9
    return 0;                        // Unknown → None
}

void PadDetailsWidget::onChokeGroupChanged(int selectionIndex)
{
    if (!hasSelection_ || currentPadId_ < 0)
        return;

    const int storage = chokeStorageFromIndex(selectionIndex);
    engine().getSampler().setChokeGroup(currentPadId_, storage);
    currentPad_.chokeGroup = storage;
}

void PadDetailsWidget::cycleTriggerMode()
{
    if (! hasSelection_ || currentPadId_ < 0 || ! currentPad_.hasSample)
        return;

    using Mode = SamplerInstrument::TriggerMode;
    const std::array singleLayerModes {
        Mode::HoldEnvelope, Mode::OneShot,
        Mode::VelocityRoundRobinOrdered, Mode::VelocityRoundRobinRandom
    };
    const std::array layeredModes {
        Mode::HoldEnvelope, Mode::OneShot,
        Mode::RoundRobinOrdered, Mode::RoundRobinRandom
    };
    const auto cycle = [this](const auto& modes)
    {
        const auto found = std::find(
            modes.begin(), modes.end(), currentPad_.triggerMode);
        if (found == modes.end() || std::next(found) == modes.end())
            return modes.front();
        return *std::next(found);
    };
    const auto mode = currentPad_.sampleLayerCount <= 1
        ? cycle(singleLayerModes) : cycle(layeredModes);
    engine().getSampler().setTriggerMode(currentPadId_, mode);
    currentPad_.triggerMode = mode;
    repaint();
}

void PadDetailsWidget::setLayerGain(double db)
{
    if (currentPadId_ < 0
        || ! juce::isPositiveAndBelow(
            selectedLayerIndex_, static_cast<int>(currentPad_.sampleLayers.size())))
        return;
    if (engine().getSampler().setSampleLayerGainDb(
            currentPadId_, selectedLayerIndex_, static_cast<float>(db)))
    {
        currentPad_.sampleLayers[static_cast<size_t>(selectedLayerIndex_)].gainDb =
            static_cast<float>(db);
        repaint();
    }
}

void PadDetailsWidget::setLayerWeight(double percent)
{
    if (currentPadId_ < 0
        || ! juce::isPositiveAndBelow(
            selectedLayerIndex_, static_cast<int>(currentPad_.sampleLayers.size())))
        return;
    const float weight = static_cast<float>(percent / 100.0);
    if (engine().getSampler().setSampleLayerRandomWeight(
            currentPadId_, selectedLayerIndex_, weight))
    {
        currentPad_.sampleLayers[static_cast<size_t>(selectedLayerIndex_)].randomWeight =
            weight;
        repaint();
    }
}

void PadDetailsWidget::setLayerVelocityCurve(int index)
{
    if (currentPadId_ < 0
        || ! juce::isPositiveAndBelow(
            selectedLayerIndex_, static_cast<int>(currentPad_.sampleLayers.size())))
        return;
    const auto curve = static_cast<SamplerInstrument::LayerVelocityCurve>(
        juce::jlimit(0,
            static_cast<int>(SamplerInstrument::LayerVelocityCurve::Fixed), index));
    if (engine().getSampler().setSampleLayerVelocityCurve(
            currentPadId_, selectedLayerIndex_, curve))
    {
        currentPad_.sampleLayers[static_cast<size_t>(selectedLayerIndex_)].velocityCurve =
            curve;
        repaint();
    }
}

void PadDetailsWidget::onGainChanged(double db)
{
    if (!hasSelection_ || currentPadId_ < 0)
        return;

    engine().getSampler().setPadSampleNormalizationGainDb(currentPadId_,
                                                          static_cast<float>(db));
    sampleGainDb_ = static_cast<float>(db);
    repaint();
}

void PadDetailsWidget::setLayerVelocityMinimum(double percent)
{
    if (! hasSelection_ || currentPadId_ < 0
        || currentPad_.sampleLayers.empty())
        return;
    auto& layer = currentPad_.sampleLayers.front();
    const float minimum = static_cast<float>(percent / 100.0);
    if (engine().getSampler().setSampleLayerVelocityRange(
            currentPadId_, 0, minimum, layer.velocityMaximum))
        layer.velocityMinimum = minimum;
    repaint();
}

void PadDetailsWidget::setLayerVelocityMaximum(double percent)
{
    if (! hasSelection_ || currentPadId_ < 0
        || currentPad_.sampleLayers.empty())
        return;
    auto& layer = currentPad_.sampleLayers.front();
    const float maximum = static_cast<float>(percent / 100.0);
    if (engine().getSampler().setSampleLayerVelocityRange(
            currentPadId_, 0, layer.velocityMinimum, maximum))
        layer.velocityMaximum = maximum;
    repaint();
}

void PadDetailsWidget::updateThumbnail()
{
    if (!currentPad_.hasSample || !currentPad_.sampleFile.existsAsFile())
    {
        thumbnail_.reset();
        loadedThumbnailPath_.clear();
        return;
    }

    const juce::String path = currentPad_.sampleFile.getFullPathName();
    if (path == loadedThumbnailPath_ && thumbnail_ != nullptr)
        return;

    thumbnail_ = std::make_unique<juce::AudioThumbnail>(256, thumbnailFormatManager_, thumbnailCache_);
    thumbnail_->setSource(new juce::FileInputSource(currentPad_.sampleFile));
    loadedThumbnailPath_ = path;
}

void PadDetailsWidget::refreshSelection()
{
    const auto previousSignature = computeVisualSignature();
    hasSelection_ = false;
    currentPadId_ = -1;
    padTitle_.clear();
    sampleStatus_.clear();
    samplePath_.clear();
    midiInfo_.clear();

    auto& padBank = engine().getSampler();
    const int selectedId = padBank.getSelectedPad();

    if (selectedId < 0)
    {
        thumbnail_.reset();
        loadedThumbnailPath_.clear();
        if (computeVisualSignature() != previousSignature)
            repaint();
        return;
    }

    auto snapshots = padBank.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::State);
    auto match = std::find_if(snapshots.begin(), snapshots.end(),
                              [selectedId](const SamplerInstrument::PadSnapshot& snapshot)
                              {
                                  return snapshot.id == selectedId;
                              });

    if (match == snapshots.end())
    {
        thumbnail_.reset();
        loadedThumbnailPath_.clear();
        if (computeVisualSignature() != previousSignature)
            repaint();
        return;
    }

    currentPad_ = *match;
    currentPadId_ = currentPad_.id;
    hasSelection_ = true;
    if (layerPadMode_ && currentPad_.sampleLayerCount <= 1)
        setLayerPadMode(false);

    // Cache the normalization gain (not in PadSnapshot).
    if (auto* info = padBank.getPad(currentPadId_))
        sampleGainDb_ = info->normalizationGainDb;

    // Cache the current instrument name (if any) for display.
    instrumentName_.clear();
    if (padBank.hasInstrument(currentPadId_))
    {
        if (auto* inst = padBank.getPadInstrument(currentPadId_))
            instrumentName_ = inst->getName();
    }

    // Cache the group accent colour.
    auto& gm = engine().getGroupManager();
    const int ag = gm.getActiveGroupIndex();
    cachedAccent_ = (ag >= 0)
        ? UiTheme::ledIndexToColour(UiTheme::brightestHueVariant(gm.getGroupColor(ag)))
        : UiTheme::kTitlebarAccent;

    // Title shows the stored pad name — falls back to "Pad N" default via
    // setPadName's empty-string reset. Rename flow mutates this via setPadName,
    // which the next refreshSelection() picks up.
    padTitle_ = padBank.getPadName(currentPadId_);
    if (padTitle_.isEmpty())
        padTitle_ = juce::String("Pad ") + juce::String(currentPadId_ + 1);

    if (currentPad_.hasSample)
    {
        const auto displayName = currentPad_.sampleName.isNotEmpty() ? currentPad_.sampleName
                                                                      : currentPad_.sampleFile.getFileNameWithoutExtension();
        sampleStatus_ = displayName.isNotEmpty() ? displayName : juce::String("Loaded");
        samplePath_ = currentPad_.sampleFile.existsAsFile() ? currentPad_.sampleFile.getFullPathName()
                                                             : juce::String("-- file not found --");
    }
    else
    {
        sampleStatus_ = "Empty";
        samplePath_ = juce::String();
    }

    midiInfo_ = "MIDI: Ch " + juce::String(currentPad_.midiChannel)
              + " / Note " + juce::String(currentPad_.midiNote)
              + "  ·  " + SamplerInstrument::triggerModeName(currentPad_.triggerMode)
              + "  ·  " + juce::String(currentPad_.sampleLayerCount)
              + (currentPad_.sampleLayerCount == 1 ? " layer" : " layers");

    updateThumbnail();
    if (computeVisualSignature() != previousSignature)
        repaint();
}

std::size_t PadDetailsWidget::computeVisualSignature() const
{
    std::size_t signature = 0;
    const auto mix = [&signature](std::size_t value)
    {
        signature ^= value + 0x9e3779b97f4a7c15ULL
                   + (signature << 6) + (signature >> 2);
    };

    mix(static_cast<std::size_t>(hasSelection_));
    mix(static_cast<std::size_t>(currentPadId_ + 1));
    mix(static_cast<std::size_t>(padTitle_.hashCode64()));
    mix(static_cast<std::size_t>(sampleStatus_.hashCode64()));
    mix(static_cast<std::size_t>(samplePath_.hashCode64()));
    mix(static_cast<std::size_t>(midiInfo_.hashCode64()));
    mix(static_cast<std::size_t>(instrumentName_.hashCode64()));
    mix(std::hash<float>{}(sampleGainDb_));
    mix(std::hash<double>{}(currentPad_.windowStartSeconds));
    mix(std::hash<double>{}(currentPad_.windowEndSeconds));
    mix(static_cast<std::size_t>(currentPad_.chokeGroup));
    mix(static_cast<std::size_t>(currentPad_.triggerMode));
    mix(static_cast<std::size_t>(currentPad_.sampleLayerCount));
    for (const auto& layer : currentPad_.sampleLayers)
    {
        mix(static_cast<std::size_t>(layer.name.hashCode64()));
        mix(std::hash<float>{}(layer.gainDb));
        mix(std::hash<float>{}(layer.randomWeight));
        mix(static_cast<std::size_t>(layer.velocityCurve));
        mix(std::hash<float>{}(layer.velocityMinimum));
        mix(std::hash<float>{}(layer.velocityMaximum));
    }
    mix(static_cast<std::size_t>(cachedAccent_.getARGB()));
    return signature;
}

void PadDetailsWidget::beginPadRename()
{
    auto* wm = windowManager();
    if (wm == nullptr)
        return;
    if (!hasSelection_ || currentPadId_ < 0)
        return;
    if (renameField_.isEditing())
        return;  // already renaming — ignore re-entries

    // PadDetailsWidget can mount on either side, so stash the opposite side
    // so the T9 host (which always opens on the opposite panel) has a free slot.
    const auto mySide = wm->getSide(this);
    const auto oppoSide = (mySide == DisplaySide::Left) ? DisplaySide::Right
                                                        : DisplaySide::Left;
    renameStashedRight_ = wm->takeWidget(oppoSide);

    const int padIdx = currentPadId_;

    TextInputComponent::Config cfg;
    cfg.prompt = "Rename pad";
    cfg.dictionaryScope = "pad-names";
    cfg.maxLength = 32;
    cfg.charFilter = [](juce::juce_wchar c) {
        return c >= 0x20 && c <= 0x7e;  // printable ASCII
    };
    cfg.initialValue = engine().getSampler().getPadName(padIdx);

    renameField_.configure(cfg);
    // Live-paint the in-flight buffer into the titlebar as the user types —
    // getTitle() returns the buffer + block cursor while isEditing() is true.
    renameField_.setOnTextChanged([this](juce::String /*live*/) {
        if (auto* w = windowManager())
            w->refreshBars();
        repaint();
    });
    renameField_.setOnCommit([this, padIdx](juce::String name) {
        engine().getSampler().setPadName(padIdx, name);
        restoreRenameStashedWidget();
        refreshSelection();
        if (auto* w = windowManager())
            w->refreshBars();
        repaint();
        showToast(ToastKind::Success, "Renamed");
    });
    renameField_.setOnCancel([this]() {
        restoreRenameStashedWidget();
    });
    renameField_.beginEdit();
}

void PadDetailsWidget::restoreRenameStashedWidget()
{
    auto* wm = windowManager();
    if (wm == nullptr)
    {
        renameStashedRight_.reset();
        return;
    }
    if (renameStashedRight_ != nullptr)
    {
        const auto mySide = wm->getSide(this);
        const auto oppoSide = (mySide == DisplaySide::Left) ? DisplaySide::Right
                                                            : DisplaySide::Left;
        wm->open(std::move(renameStashedRight_), oppoSide);
    }
}
