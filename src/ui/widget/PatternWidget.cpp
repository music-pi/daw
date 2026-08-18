#include "PatternWidget.h"

#include <algorithm>
#include <cmath>

#include "../../engine/SamplerInstrument.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/GroupManager.h"
#include "../../engine/KeyboardInstrumentBank.h"
#include "../../engine/MidiConstants.h"
#include "../../engine/commands/FillEuclideanCommand.h"
#include "../../engine/commands/ToggleSequencerStepCommand.h"
#include "../../engine/commands/SetStepVelocityCommand.h"
#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../hw/HardwareState.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"
#include "ConfirmDialog.h"
#include "WindowManager.h"

namespace
{
juce::String formatTime(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        seconds = 0.0;

    const auto totalMillis = static_cast<int>(std::round(seconds * 1000.0));
    const auto totalSeconds = totalMillis / 1000;
    const auto millis = totalMillis % 1000;
    const auto minutes = totalSeconds / 60;
    const auto secs = totalSeconds % 60;

    return juce::String::formatted("%02d:%02d.%03d", minutes, secs, millis);
}

juce::Colour statusColourFor(const AudioEngine::TransportSnapshot& snapshot)
{
    if (!snapshot.hasEdit)
        return juce::Colours::darkgrey;
    if (snapshot.isRecording)
        return juce::Colours::red;
    if (snapshot.isPlaying)
        return juce::Colours::limegreen;
    if (snapshot.isPaused)
        return juce::Colours::yellow;
    if (snapshot.isLooping)
        return juce::Colours::orange;
    return juce::Colours::grey;
}
} // namespace

PatternWidget::PatternWidget()
{
    // The field is a T9 driver, not a rendered component. Keeping it as a
    // child but invisible matches FileBrowserWidget's pattern and hands it a
    // well-defined parent for findEnclosingWidget() lookups.
    addChildComponent(renameField_);
}

PatternWidget::~PatternWidget() = default;

// -- Widget identity --

WidgetDescriptor PatternWidget::describe() const
{
    return { "pattern", 1, false, DisplayConstraint::Any };
}

// -- Lifecycle --

void PatternWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    transportSnapshot_ = engine().getTransportSnapshot();
    lastCursorStep_ = -1;

    // Sync local pattern number with the sampler's active pattern so reopening
    // the widget picks up whatever was playing last.
    currentPatternNumber_ = engine().getSampler().getActivePatternIndex();

    autoSelectFirstChannel();

    auto ownerId = describe().id.toStdString();
    hw().setLed("pattern", HardwareConstants::kLedBright, ownerId);

    refreshPadLedState();
    updateStepButtonLed();
}

void PatternWidget::onDeactivated()
{
    lastCursorStep_ = -1;
    selectedStep_ = -1;
    selectHeld_ = false;
    replaceMode_ = false;
    velocityKnobTransactionOpen_ = false;

    // Clear callbacks before ending edit so nothing fires during teardown —
    // cancel would pop the stashed widget back into a panel we no longer
    // own. Drop the stash outright instead.
    renameField_.setOnCommit(nullptr);
    renameField_.setOnCancel(nullptr);
    renameField_.setOnTextChanged(nullptr);
    if (renameField_.isEditing())
        renameField_.endEdit(false);
    renameStashedRight_.reset();
}

// -- Resources --

std::vector<std::string> PatternWidget::requiredResources(int page)
{
    if (page != 0)
        return {};

    std::vector<std::string> resources;

    // Pad LEDs p1-p16
    for (int i = 1; i <= 16; ++i)
        resources.push_back("p" + std::to_string(i));

    // Screen button and step button LED
    resources.push_back("pattern");
    resources.push_back("step");

    // Option buttons and knobs depend on panel offset
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

std::vector<Option> PatternWidget::getOptions(int page)
{
    if (page != 0)
        return {};

    auto& sampler = engine().getSampler();
    const bool shift = controllerHost() != nullptr && controllerHost()->isShiftPressed();

    // d1 / d2 under shift — d1 becomes a dedicated "Rename" option and d2
    // stays empty. Non-shift is the usual span-2 Pattern {prev | next}
    // navigator that shows the current pattern name.
    Option d1;
    Option d2;
    if (shift)
    {
        d1.id = "pattern.rename";
        d1.label = "Rename";
        d1.state = OptionState::Enabled;
        d1.onInvoke = [this]() { beginPatternRename(); };

        d2.id = "pattern.rename.empty";
        d2.state = OptionState::Empty;
    }
    else
    {
        d1.id = "pattern.navigator";
        d1.label = renameField_.isEditing()
            ? renameField_.getText() + juce::String::charToString((juce::juce_wchar) 0x2588)
            : sampler.getPatternName(currentPatternNumber_);
        d1.span = 2;
        d1.isOperationNavigator = true;
        d1.state = OptionState::Enabled;
        d1.onOperationNavigatorPart = [this](bool isLeftPart)
        {
            if (isLeftPart)
                selectPrevPattern();
            else
                selectNextPattern();
        };
    }

    // D3 opens the detailed note editor in keyboard/instrument mode. Step
    // mode keeps its existing Euclidean-fill operation in the same slot.
    Option euclid;
    const bool canOpenPianoRoll = channelMode_ == ChannelMode::Instruments
                               && ! isStepModeActive()
                               && selectedChannelPadId_ >= 0
                               && static_cast<bool>(onPianoRollRequested_);
    const bool canEuclid = isStepModeActive()
                        && selectedChannelPadId_ >= 0
                        && selectedPatternIndex_ >= 0;
    if (canOpenPianoRoll)
    {
        euclid.id = "pattern.piano_roll";
        euclid.label = "Piano Roll";
        euclid.state = OptionState::Enabled;
        euclid.onInvoke = [this]() { onPianoRollRequested_(); };
    }
    else if (canEuclid)
    {
        euclid.id = "pattern.euclid";
        euclid.label = "Euclid";
        euclid.state = OptionState::Enabled;
        euclid.onInvoke = [this]() {
            const int steps = getStepCount();
            if (steps <= 0) return;
            auto& um = engine().getUndoManager();
            um.beginNewTransaction("Euclidean fill");
            um.perform(new FillEuclideanCommand(
                engine(), selectedPatternIndex_, selectedChannelPadId_,
                /*pulses*/  juce::jmax(1, steps / 2),
                /*rotation*/ 0));
            refreshPadLedState();
            repaint();
            showToast(ToastKind::Success,
                      "Euclid " + juce::String(juce::jmax(1, steps / 2))
                      + "/" + juce::String(steps));
        };
    }
    else
    {
        euclid.id = "pattern.euclid.empty";
        euclid.state = OptionState::Empty;
    }

    Option destructive;
    destructive.id = "pattern.destructive";
    destructive.label = shift ? "Delete" : "Clear";
    destructive.state = OptionState::Enabled;
    destructive.onInvoke = [this]() {
        // Re-read shift at invoke time so the action matches the press.
        const bool isShift = controllerHost() != nullptr && controllerHost()->isShiftPressed();
        auto* wm = windowManager();
        if (wm == nullptr)
            return;

        const int patternIdx = currentPatternNumber_;
        if (isShift)
        {
            auto dialog = std::make_unique<ConfirmDialog>(
                juce::String("Delete Pattern"),
                juce::String("Delete pattern ") + juce::String(patternIdx + 1) + "?",
                juce::String("Delete"),
                [this, patternIdx]() {
                    auto& sampler = engine().getSampler();
                    sampler.deletePattern(patternIdx);
                    currentPatternNumber_ = sampler.getActivePatternIndex();
                    if (selectedChannelPadId_ >= 0)
                        selectChannel(selectedChannelPadId_);
                    engine().updateLoopRangeForPlayMode();
                    refreshPadLedState();
                    repaint();
                    showToast(ToastKind::Success, "Pattern deleted");
                });
            wm->showDialog(std::move(dialog));
        }
        else
        {
            // Scope Clear to the view the user is looking at: pads view clears
            // drums, Instruments view clears keyboard slots. Clearing both at
            // once was the prior behaviour and felt like collateral damage.
            const bool keyboardView = channelMode_ == ChannelMode::Instruments;
            const juce::String scopeLabel = keyboardView ? "keys" : "drums";
            auto dialog = std::make_unique<ConfirmDialog>(
                juce::String("Clear Pattern"),
                juce::String("Clear ") + scopeLabel + " in pattern "
                    + juce::String(patternIdx + 1) + "?",
                juce::String("Clear"),
                [this, patternIdx, keyboardView]() {
                    if (keyboardView)
                        engine().getKeyboardBank().clearPattern(patternIdx);
                    else
                        engine().getSampler().clearPattern(patternIdx);
                    refreshPadLedState();
                    repaint();
                    showToast(ToastKind::Success, "Pattern cleared");
                });
            wm->showDialog(std::move(dialog));
        }
    };

    // Under shift the d1/d2 pair takes two slots (Rename + Empty); otherwise
    // d1 is the span-2 navigator that occupies both slots on its own.
    if (shift)
        return { std::move(d1), std::move(d2), std::move(euclid), std::move(destructive) };
    return { std::move(d1), std::move(euclid), std::move(destructive) };
}

std::vector<Knob> PatternWidget::getKnobs(int page)
{
    if (page != 0)
        return {};

    // K1: Bars per pattern
    Knob barsKnob;
    barsKnob.id = "bars";
    barsKnob.label = "Bars";
    barsKnob.isEnabled = true;
    barsKnob.model = Knob::NumericModel{
        .value = static_cast<double>(barsPerPattern_),
        .minimum = 1.0,
        .maximum = 16.0,
        .step = 1.0,
        .formatter = [](double v) { return juce::String(static_cast<int>(v)); }
    };

    // K2: Steps per bar (resolution)
    Knob stepsKnob;
    stepsKnob.id = "steps_per_bar";
    stepsKnob.label = "Steps/Bar";
    stepsKnob.isEnabled = true;
    stepsKnob.model = Knob::NumericModel{
        .value = static_cast<double>(stepsPerBar_),
        .minimum = 1.0,
        .maximum = 16.0,
        .step = 1.0,
        .formatter = [](double v) { return juce::String(static_cast<int>(v)); }
    };

    // K3: empty slot (reserved)
    Knob emptyKnob;
    emptyKnob.id = "pattern_empty";
    emptyKnob.isEnabled = false;

    // K4: velocity of selected step (only enabled while a step is selected)
    Knob velKnob;
    velKnob.id = "step_velocity";
    velKnob.label = selectedStep_ >= 0
                        ? ("Vel S" + juce::String(selectedStep_ + 1))
                        : juce::String("Velocity");
    velKnob.isEnabled = selectedStep_ >= 0;

    int velValue = 0;
    if (selectedStep_ >= 0 && selectedChannelPadId_ >= 0)
    {
        auto snapshots = engine().getSampler().getPadsSnapshot(
            SamplerInstrument::SnapshotContent::Patterns);
        for (const auto& pad : snapshots)
        {
            if (pad.id != selectedChannelPadId_
                || static_cast<int>(pad.patterns.size()) <= currentPatternNumber_)
                continue;
            const auto& p = pad.patterns[static_cast<size_t>(currentPatternNumber_)];
            if (selectedStep_ < static_cast<int>(p.velocities.size()))
                velValue = p.velocities[static_cast<size_t>(selectedStep_)];
            break;
        }
    }
    velKnob.model = Knob::NumericModel{
        .value = static_cast<double>(velValue),
        .minimum = 0.0,
        .maximum = 127.0,
        .step = 1.0,
        .formatter = [](double v) { return juce::String(static_cast<int>(v)); }
    };
    velKnob.onInputResolved = [this](double) {
        velocityKnobTransactionOpen_ = false;
    };

    return { barsKnob, stepsKnob, emptyKnob, velKnob };
}

// -- Rendering --

void PatternWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void PatternWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (page != 0)
        return;

    // Title bar — matches MK3 display design: accent | title | info right-aligned
    auto titleArea = bounds.removeFromTop(UiTheme::kTitlebarHeight);
    {
        g.setColour(UiTheme::kTitlebarBackground);
        g.fillRect(titleArea);

        // Accent bar (status colour)
        auto accentColour = statusColourFor(transportSnapshot_);
        auto accentRect = titleArea.removeFromLeft(UiTheme::kAccentWidth);
        g.setColour(accentColour);
        g.fillRect(accentRect);

        auto inner = titleArea.reduced(6, 0);

        // Title (left) — show current pattern number next to the view name.
        // A red dot sits to the left of the title whenever record is armed,
        // so the UI mirrors the recCountIn LED at a glance.
        const bool armed      = engine().isRecordArmed();
        const bool countingIn = engine().isCountingIn();
        const bool activeRec  = armed && transportSnapshot_.isPlaying && !countingIn;

        if (armed)
        {
            constexpr float dotSize = 8.0f;
            auto dotArea = inner.removeFromLeft(static_cast<int>(dotSize) + 6);
            auto dot = juce::Rectangle<float>(
                static_cast<float>(dotArea.getX() + 1),
                static_cast<float>(dotArea.getCentreY()) - dotSize * 0.5f,
                dotSize,
                dotSize);
            // Dim the dot during count-in so it reads as "pending" vs
            // "actively writing" (fully lit). The LED blink already carries
            // the motion; the dot is a static companion.
            g.setColour(countingIn ? juce::Colour(0xffaa2222) : juce::Colour(0xffee3b3b));
            g.fillEllipse(dot);
        }

        // Show the live T9 buffer during rename so the user sees what they're
        // typing; a trailing block-cursor makes it read as an input field.
        const bool renaming = renameField_.isEditing();
        juce::String titleText;
        if (renaming)
        {
            const auto live = renameField_.getText();
            titleText = live + juce::String::charToString((juce::juce_wchar) 0x2588);
            g.setColour(UiTheme::kTitlebarAccent);
        }
        else
        {
            titleText = engine().getSampler().getPatternName(currentPatternNumber_);
            g.setColour(juce::Colours::white);
        }
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kTitlebarTitle, juce::Font::bold)));
        g.drawText(titleText, inner.removeFromLeft(160), juce::Justification::centredLeft, false);

        // Info area (right-aligned): status · position · tempo.
        // Active recording / count-in status is rendered separately so it can
        // be bolded + coloured red without bleeding into the rest of the row.
        juce::String statusText;
        juce::Colour statusColour = juce::Colours::lightgrey;
        juce::Font statusFont { juce::FontOptions(UiTheme::Fonts::kTitlebarSubtitle) };

        if (!transportSnapshot_.hasEdit)
        {
            statusText = "No Edit";
        }
        else if (countingIn)
        {
            statusText = "Count-in";
            statusColour = juce::Colour(0xffee3b3b);
            statusFont = juce::Font(juce::FontOptions(UiTheme::Fonts::kTitlebarSubtitle,
                                                      juce::Font::bold));
        }
        else if (activeRec)
        {
            statusText = replaceMode_ ? "Overdub" : "Recording";
            statusColour = juce::Colour(0xffee3b3b);
            statusFont = juce::Font(juce::FontOptions(UiTheme::Fonts::kTitlebarSubtitle,
                                                      juce::Font::bold));
        }
        else if (armed)
        {
            statusText = "Armed";
            statusColour = juce::Colour(0xffee3b3b);
            statusFont = juce::Font(juce::FontOptions(UiTheme::Fonts::kTitlebarSubtitle,
                                                      juce::Font::bold));
        }
        else if (transportSnapshot_.isPlaying)
        {
            statusText = "Playing";
        }
        else
        {
            statusText = "Stopped";
        }

        juce::String positionText = transportSnapshot_.hasEdit
                                        ? formatTime(transportSnapshot_.positionSeconds)
                                        : "--:--";

        const bool hasTempo = transportSnapshot_.hasEdit && transportSnapshot_.tempoBpm > 0.0;
        juce::String tempoText = hasTempo ? juce::String(transportSnapshot_.tempoBpm, 1) + " BPM"
                                          : "-- BPM";

        juce::String tailText = "  ·  " + positionText + "  ·  " + tempoText;
        const int swingInt = juce::roundToInt(transportSnapshot_.swingPercent);
        if (swingInt != 50)
            tailText += "  ·  SW " + juce::String(swingInt) + "%";

        // Draw the tail (non-coloured) first, right-aligned, then the status
        // string right-aligned to the left of the tail so colours compose.
        juce::Font tailFont { juce::FontOptions(UiTheme::Fonts::kTitlebarSubtitle) };
        g.setFont(tailFont);
        g.setColour(juce::Colours::lightgrey);
        const int tailWidth = juce::GlyphArrangement::getStringWidthInt(tailFont, tailText);
        g.drawText(tailText,
                   inner.removeFromRight(tailWidth + 2),
                   juce::Justification::centredRight,
                   false);

        g.setFont(statusFont);
        g.setColour(statusColour);
        g.drawText(statusText, inner, juce::Justification::centredRight, false);
    }

    auto contentArea = bounds.reduced(8).withTrimmedTop(4);

    int cols = getStepCount();
    if (cols <= 0)
        cols = 1;

    // A Row abstracts a pattern-view channel so the paint loop works for
    // both drum-kit rows (sample pads, step display) and instrument rows
    // (keyboard-bank slots, melodic display).
    struct Row
    {
        int id;                // pad id (pads mode) OR bank slot index (instruments mode)
        juce::String name;
        te::MidiClip* midiClip { nullptr };
        const SamplerInstrument::PadSnapshot::PatternSnapshot* padPattern { nullptr };
        bool isInstrument { false };
    };

    std::vector<Row> rowList;
    // Snapshot owner must outlive rowList — Row.padPattern is a pointer into
    // this vector, so it lives at function scope, not inside the else-branch
    // that constructs it. Leaving it scoped-local to the else was a latent
    // use-after-free that only materialised once a pad actually had a sample.
    std::vector<SamplerInstrument::PadSnapshot> padSnapshots;

    if (channelMode_ == ChannelMode::Instruments)
    {
        auto& bank = engine().getKeyboardBank();
        for (int i = 0; i < bank.getNumSlots(); ++i)
        {
            const auto* s = bank.getSlot(i);
            if (s == nullptr) continue;
            Row r;
            r.id = i;
            r.name = s->displayName.isNotEmpty() ? s->displayName
                                                 : juce::String("Instrument ") + juce::String(i + 1);
            r.midiClip = s->midiClip.get();
            r.isInstrument = true;
            rowList.push_back(std::move(r));
        }
    }
    else
    {
        padSnapshots = engine().getSampler().getPadsSnapshot(
            SamplerInstrument::SnapshotContent::Patterns);
        for (const auto& snap : padSnapshots)
        {
            if (!snap.hasSample) continue;
            Row r;
            r.id = snap.id;
            r.name = snap.name;
            r.isInstrument = false;
            if (static_cast<int>(snap.patterns.size()) > currentPatternNumber_)
                r.padPattern = &snap.patterns[static_cast<size_t>(currentPatternNumber_)];
            rowList.push_back(std::move(r));
        }
    }

    const int rows = static_cast<int>(rowList.size());

    // Resolve group colour
    juce::Colour groupColour = UiTheme::kPadWithSample;
    auto& gm = engine().getGroupManager();
    int ag = gm.getActiveGroupIndex();
    if (ag >= 0)
        groupColour = UiTheme::ledIndexToColour(gm.getGroupColor(ag));

    const auto offColour = UiTheme::kPadEmpty;
    const double playheadStep = getPlayheadStep();

    float gridHeightUsed = static_cast<float>(contentArea.getHeight());
    stepCells_.clear();
    stepCells_.reserve(static_cast<size_t>(rows * cols));

    if (rows == 0)
    {
        g.setColour(juce::Colours::grey);
        g.drawFittedText("Load a sample onto a pad to view its pattern",
                         contentArea,
                         juce::Justification::centred,
                         1);
    }
    else
    {
        const int labelColumnWidth = juce::jmin(160, juce::jmax(100, contentArea.getWidth() / 6));
        const auto labelColumn = contentArea.removeFromLeft(labelColumnWidth);
        const auto gridArea = contentArea;

        const float cellWidth = gridArea.getWidth() / static_cast<float>(cols);
        constexpr int kPadSlotCount = 16;
        const float maxRowHeight = gridArea.getHeight() / static_cast<float>(kPadSlotCount);
        const float sequentialRowHeight = gridArea.getHeight() / static_cast<float>(rows);
        const float rowHeight = std::min(maxRowHeight, sequentialRowHeight);

        const int highlightCol = juce::jlimit(0, cols - 1, static_cast<int>(std::floor(playheadStep)));

        int rowIndex = 0;
        for (const auto& row : rowList)
        {
            // Per-row data for instrument rows: any-note mask + note-start
            // pitches so we can draw the note name at the start of each note.
            juce::BigInteger instrumentRowMask;
            std::vector<int> instrumentNoteStartPitch(static_cast<size_t>(cols), -1);
            if (row.isInstrument && row.midiClip != nullptr)
            {
                const double stepBeats = 4.0 / juce::jmax(1, stepsPerBar_);
                for (auto* n : row.midiClip->getSequence().getNotes())
                {
                    const double ns = n->getStartBeat().inBeats();
                    const double ne = ns + n->getLengthBeats().inBeats();
                    const int first = juce::jmax(0,
                        static_cast<int>(ns / stepBeats));
                    const int last = juce::jmin(cols - 1,
                        static_cast<int>((ne - 1e-6) / stepBeats));
                    for (int sc = first; sc <= last; ++sc)
                        instrumentRowMask.setBit(sc);
                    if (first >= 0 && first < cols
                        && instrumentNoteStartPitch[(size_t) first] < 0)
                        instrumentNoteStartPitch[(size_t) first] = n->getNoteNumber();
                }
            }

            const float yPosition = static_cast<float>(gridArea.getY()) + rowIndex * rowHeight;

            juce::Rectangle<float> labelRect(
                static_cast<float>(labelColumn.getX()),
                yPosition,
                static_cast<float>(labelColumnWidth),
                rowHeight);

            const bool isSelected = (row.id == selectedChannelPadId_);
            g.setColour(isSelected ? juce::Colours::white : juce::Colours::lightgrey);
            auto labelFont = g.getCurrentFont();
            labelFont.setBold(isSelected);
            g.setFont(labelFont);
            g.drawFittedText(row.name,
                             labelRect.toNearestInt().reduced(4),
                             juce::Justification::centredLeft,
                             1);

            for (int c = 0; c < cols; ++c)
            {
                juce::Rectangle<float> cell(
                    static_cast<float>(gridArea.getX()) + c * cellWidth + 1.0f,
                    yPosition + 1.0f,
                    cellWidth - 2.0f,
                    rowHeight - 2.0f);

                const bool lit = row.isInstrument
                    ? instrumentRowMask[c]
                    : (row.padPattern != nullptr ? row.padPattern->steps[c] : false);

                auto colour = lit ? groupColour : offColour;

                const bool isBarStart = (c % 4) == 0;
                if (isBarStart)
                    colour = colour.brighter(lit ? 0.15f : 0.35f);

                if (c == highlightCol)
                    colour = colour.brighter(lit ? 0.4f : 0.8f);

                g.setColour(colour);
                if (row.isInstrument)
                    g.fillRect(cell);
                else
                    g.fillRoundedRectangle(cell, 4.0f);

                if (row.isInstrument && lit)
                {
                    const int startPitch = instrumentNoteStartPitch[(size_t) c];
                    if (startPitch >= 0)
                    {
                        static const char* kNames[] = {
                            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                        const int pc  = ((startPitch % 12) + 12) % 12;
                        const int oct = (startPitch / 12) - 1;
                        const juce::String label =
                            juce::String(kNames[pc]) + juce::String(oct);
                        auto prevFont = g.getCurrentFont();
                        auto small = prevFont;
                        small.setHeight(juce::jmin(11.0f, cell.getHeight() - 2.0f));
                        g.setFont(small);
                        g.setColour(juce::Colours::black.withAlpha(0.85f));
                        g.drawFittedText(label, cell.toNearestInt().reduced(2),
                                         juce::Justification::centredLeft, 1);
                        g.setFont(prevFont);
                    }
                }

                const bool isSelectedStep = row.id == selectedChannelPadId_ && c == selectedStep_;
                if (isSelectedStep)
                {
                    g.setColour(juce::Colours::white);
                    g.drawRoundedRectangle(cell, 4.0f, 1.5f);
                }

                if (! row.isInstrument && row.padPattern != nullptr)
                {
                    stepCells_.push_back({
                        row.id,
                        row.padPattern->patternIndex,
                        c,
                        cell,
                        lit
                    });
                }
            }

            ++rowIndex;
        }

        gridHeightUsed = rowHeight * static_cast<float>(rows);
    }

    // Playhead line
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    const auto gridOriginX = static_cast<float>(contentArea.getX());
    const auto gridOriginY = static_cast<float>(contentArea.getY());
    const float effectiveCellWidth = rows > 0 ? contentArea.getWidth() / static_cast<float>(cols)
                                              : contentArea.getWidth();
    const float playheadX = gridOriginX + static_cast<float>(playheadStep) * effectiveCellWidth;
    g.fillRect(juce::Rectangle<float>(playheadX,
                                       gridOriginY,
                                       2.0f,
                                       rows > 0 ? std::min(gridHeightUsed, static_cast<float>(contentArea.getHeight())) : 0.0f));
}

void PatternWidget::resized()
{
    contentArea_ = getLocalBounds();
}

// -- Input --

void PatternWidget::handlePad(const controller_events::PadEvent& e)
{
    if (!e.pressed)
        return;

    int padIndex = static_cast<int>(e.pad);
    if (padIndex < 0 || padIndex >= 16)
        return;

    // Step mode has exclusive use of the pads as step positions. Record-arm
    // must not reinterpret the same press as a live hit at the playhead.
    if (!stepModeActive_ && engine().isRecordArmed() && engine().isPlaying())
    {
        // Record mode: quantize pad hit to nearest step
        recordStepAtPlayhead(padIndex);
        return;
    }

    if (selectedChannelPadId_ >= 0 && selectedPatternIndex_ >= 0)
    {
        if (selectHeld_)
        {
            // Select + pad: pick step for velocity editing. step N = pad N.
            const int stepCount = getStepCount();
            if (stepCount <= 0)
                return;
            selectedStep_ = padIndex % stepCount;
            repaint();
            return;
        }

        // Normal mode: toggle step at pad position. ControllerHost already
        // dispatches hardware input onto the message thread, so another async
        // hop only makes the edit lag behind the physical press.
        toggleStepAtPad(padIndex);
    }
}

void PatternWidget::handleButton(const controller_events::ButtonEvent& e)
{
    if (e.name == "select")
    {
        selectHeld_ = e.pressed;
        return;
    }

    if (!e.pressed)
        return;

    if (e.name == "eraseReplace")
    {
        if (e.shift)
        {
            // Shift+erase: toggle replace mode (only meaningful when recording)
            if (engine().isRecordArmed())
            {
                replaceMode_ = !replaceMode_;
                showToast(ToastKind::Info, replaceMode_ ? "Replace ON" : "Replace OFF");
            }
        }
        else
        {
            // Erase: clear selected channel
            eraseSelectedChannel();
            showToast(ToastKind::Success, "Channel cleared");
        }
        return;
    }

    if (e.name == "tapMetro" && e.shift)
    {
        bool click = engine().isClickEnabled();
        engine().enableClick(!click);
        showToast(ToastKind::Info, !click ? "Metronome ON" : "Metronome OFF");
        return;
    }

    if (e.name == "navUp")
        navigateChannel(-1);
    else if (e.name == "navDown")
        navigateChannel(1);
}

void PatternWidget::handleKnob(int localIndex, int16_t delta, uint16_t /*absolute*/, bool /*shift*/)
{
    // K1-K3: coarse controls, throttle to every 3rd tick
    if (localIndex >= 0 && localIndex < 3)
    {
        knobAccumulator_[localIndex] += delta;
        if (std::abs(knobAccumulator_[localIndex]) < 3)
            return;
        delta = knobAccumulator_[localIndex] > 0 ? 1 : -1;
        knobAccumulator_[localIndex] = 0;
    }

    if (localIndex == 0)
    {
        // Bars per pattern
        int newBars = juce::jlimit(1, 16, barsPerPattern_ + static_cast<int>(delta));
        if (newBars != barsPerPattern_)
        {
            barsPerPattern_ = newBars;
            engine().getSampler().setPatternLength(currentPatternNumber_, barsPerPattern_, stepsPerBar_);
            engine().updateLoopRangeForPlayMode();
            refreshPadLedState();
            repaint();
        }
    }
    else if (localIndex == 1)
    {
        // Steps per bar (resolution)
        int newSteps = juce::jlimit(1, 16, stepsPerBar_ + static_cast<int>(delta));
        if (newSteps != stepsPerBar_)
        {
            stepsPerBar_ = newSteps;
            engine().getSampler().setPatternLength(currentPatternNumber_, barsPerPattern_, stepsPerBar_);
            engine().updateLoopRangeForPlayMode();
            refreshPadLedState();
            repaint();
        }
    }
    else if (localIndex == 3)
    {
        // K4: velocity of selected step (1..127)
        if (selectedStep_ < 0 || selectedChannelPadId_ < 0 || selectedPatternIndex_ < 0)
            return;

        knobAccumulator_[3] += delta;
        const int unit = 2;
        if (std::abs(knobAccumulator_[3]) < unit)
            return;
        const int step = knobAccumulator_[3] > 0 ? 1 : -1;
        knobAccumulator_[3] = 0;

        auto& sampler = engine().getSampler();
        auto snapshots = sampler.getPadsSnapshot(
            SamplerInstrument::SnapshotContent::Patterns);
        int currentVel = 83;
        for (const auto& pad : snapshots)
        {
            if (pad.id != selectedChannelPadId_
                || static_cast<int>(pad.patterns.size()) <= currentPatternNumber_)
                continue;
            const auto& p = pad.patterns[static_cast<size_t>(currentPatternNumber_)];
            if (selectedStep_ < static_cast<int>(p.velocities.size()))
                currentVel = p.velocities[static_cast<size_t>(selectedStep_)];
            break;
        }

        const int newVel = juce::jlimit(midi::kVelocityLiveMin, midi::kVelocityMax, currentVel + step);
        if (newVel != currentVel)
        {
            auto& undoManager = engine().getUndoManager();
            if (! velocityKnobTransactionOpen_)
            {
                undoManager.beginNewTransaction("Step velocity");
                velocityKnobTransactionOpen_ = true;
            }
            undoManager.perform(new SetStepVelocityCommand(
                engine(), selectedPatternIndex_, selectedChannelPadId_,
                selectedStep_, newVel));
            refreshPadLedState();
            repaint();
        }
    }
}

// -- Private --

void PatternWidget::onUiHostTick()
{
    transportSnapshot_ = engine().getTransportSnapshot();

    const int cursorStep = static_cast<int>(std::floor(getPlayheadStep()));
    if (cursorStep != lastCursorStep_)
    {
        lastCursorStep_ = cursorStep;
        refreshPadLedState();
    }

    // Only repaint when something visually observable has actually changed.
    // Burning 30 repaints/sec on an unchanging grid hogs the GUI thread.
    const auto sig = computeRepaintSignature();
    if (sig != lastRepaintSignature_)
    {
        lastRepaintSignature_ = sig;
        repaint();
    }
}

std::size_t PatternWidget::computeRepaintSignature() const
{
    // Hash the fields that, if changed, require a repaint. Transport fields
    // (playing, positionSeconds, tempo, recording flags) plus selection and
    // pattern shape. Position is quantised to 10 ms below, finer than the
    // 60 Hz host tick, so playback can animate smoothly without duplicate
    // repaints.
    std::size_t h = 0;
    auto mix = [&h](std::size_t v) {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };

    mix(static_cast<std::size_t>(currentPatternNumber_));
    mix(static_cast<std::size_t>(selectedChannelPadId_ + 1));
    mix(static_cast<std::size_t>(selectedPatternIndex_ + 1));
    mix(static_cast<std::size_t>(selectedStep_ + 1));
    mix(static_cast<std::size_t>(barsPerPattern_));
    mix(static_cast<std::size_t>(stepsPerBar_));
    mix(static_cast<std::size_t>(lastCursorStep_ + 1));
    mix(static_cast<std::size_t>(static_cast<int>(channelMode_)));
    mix(static_cast<std::size_t>(replaceMode_ ? 1 : 0));
    mix(static_cast<std::size_t>(engine().isRecordArmed() ? 1 : 0));
    mix(static_cast<std::size_t>(engine().isCountingIn() ? 1 : 0));
    mix(static_cast<std::size_t>(transportSnapshot_.isPlaying ? 1 : 0));
    mix(static_cast<std::size_t>(transportSnapshot_.isRecording ? 1 : 0));
    mix(static_cast<std::size_t>(transportSnapshot_.isPaused ? 1 : 0));
    mix(static_cast<std::size_t>(transportSnapshot_.isLooping ? 1 : 0));
    mix(static_cast<std::size_t>(transportSnapshot_.hasEdit ? 1 : 0));
    mix(static_cast<std::size_t>(
        std::llround(transportSnapshot_.tempoBpm * 10.0)));
    mix(static_cast<std::size_t>(
        std::llround(transportSnapshot_.swingPercent)));
    mix(static_cast<std::size_t>(
        std::llround(transportSnapshot_.positionSeconds * 100.0)));
    // Titlebar rename cursor toggles each keystroke — if renaming, mix in
    // the live buffer so the block cursor / in-flight text repaint.
    if (renameField_.isEditing())
        mix(static_cast<std::size_t>(renameField_.getText().hashCode64()));

    return h;
}

void PatternWidget::toggleStepAtPad(int padIndex)
{
    if (selectedChannelPadId_ < 0 || selectedPatternIndex_ < 0)
        return;

    const int stepCount = getStepCount();
    if (stepCount <= 0)
        return;

    const int stepIndex = padIndex % stepCount;

    // Determine current step state
    auto& padBank = engine().getSampler();
    auto snapshots = padBank.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    bool currentState = false;

    for (const auto& pad : snapshots)
    {
        if (pad.id != selectedChannelPadId_
            || static_cast<int>(pad.patterns.size()) <= currentPatternNumber_)
            continue;
        currentState = pad.patterns[static_cast<size_t>(currentPatternNumber_)].steps[stepIndex];
        break;
    }

    // Toggle via undoable command
    auto* edit = engine().getEdit();
    if (edit == nullptr)
        return;

    auto& um = edit->getUndoManager();
    um.beginNewTransaction("Toggle Step");
    um.perform(new ToggleSequencerStepCommand(engine(), selectedPatternIndex_,
                                               selectedChannelPadId_, stepIndex, !currentState));

    refreshPadLedState();
    repaint();
}

void PatternWidget::selectChannel(int padId)
{
    auto& padBank = engine().getSampler();
    auto snapshots = padBank.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);

    for (const auto& pad : snapshots)
    {
        if (pad.id != padId)
            continue;
        if (static_cast<int>(pad.patterns.size()) > currentPatternNumber_)
        {
            selectedChannelPadId_ = padId;
            selectedPatternIndex_ = pad.patterns[static_cast<size_t>(currentPatternNumber_)].patternIndex;
            selectedStep_ = -1;
            updateStepButtonLed();
            refreshPadLedState();
            repaint();
            return;
        }
    }

    selectedChannelPadId_ = -1;
    selectedPatternIndex_ = -1;
    selectedStep_ = -1;
    updateStepButtonLed();
    refreshPadLedState();
    repaint();
}

void PatternWidget::toggleStepMode()
{
    stepModeActive_ = !stepModeActive_;

    if (!stepModeActive_)
    {
        // Turn off step mode
        selectedChannelPadId_ = -1;
        selectedPatternIndex_ = -1;
    }
    else
    {
        // Turn on step mode — select first available channel
        autoSelectFirstChannel();
    }

    updateStepButtonLed();
    refreshPadLedState();
    repaint();
}

void PatternWidget::navigateChannel(int direction)
{
    std::vector<int> loadedPadIds;
    if (channelMode_ == ChannelMode::Instruments)
    {
        auto& bank = engine().getKeyboardBank();
        for (int i = 0; i < bank.getNumSlots(); ++i)
            loadedPadIds.push_back(i);
    }
    else
    {
        auto& padBank = engine().getSampler();
        for (const auto& pad : padBank.getPadsSnapshot(
                 SamplerInstrument::SnapshotContent::Patterns))
            if (pad.hasSample && ! pad.patterns.empty())
                loadedPadIds.push_back(pad.id);
    }

    if (loadedPadIds.empty())
        return;

    auto it = std::find(loadedPadIds.begin(), loadedPadIds.end(), selectedChannelPadId_);
    int currentIndex = (it != loadedPadIds.end())
                           ? static_cast<int>(std::distance(loadedPadIds.begin(), it))
                           : -1;

    int newIndex = currentIndex + direction;
    int count = static_cast<int>(loadedPadIds.size());
    newIndex = ((newIndex % count) + count) % count;

    selectChannel(loadedPadIds[static_cast<size_t>(newIndex)]);
}

void PatternWidget::cycleSelectedStep(int direction)
{
    if (selectedChannelPadId_ < 0 || direction == 0)
        return;

    auto snapshots = engine().getSampler().getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    const SamplerInstrument::PadSnapshot* selected = nullptr;
    for (const auto& pad : snapshots)
    {
        if (pad.id == selectedChannelPadId_
            && static_cast<int>(pad.patterns.size()) > currentPatternNumber_)
        {
            selected = &pad;
            break;
        }
    }
    if (selected == nullptr)
        return;

    std::vector<int> activeSteps;
    const auto& pattern = selected->patterns[static_cast<size_t>(currentPatternNumber_)];
    const int stepCount = getStepCount();
    activeSteps.reserve(static_cast<size_t>(stepCount));
    for (int i = 0; i < stepCount; ++i)
    {
        if (pattern.steps[i])
            activeSteps.push_back(i);
    }

    if (activeSteps.empty())
    {
        selectedStep_ = -1;
        repaint();
        return;
    }

    int currentIdx = -1;
    for (int i = 0; i < static_cast<int>(activeSteps.size()); ++i)
    {
        if (activeSteps[static_cast<size_t>(i)] == selectedStep_)
        {
            currentIdx = i;
            break;
        }
    }

    int newIdx;
    if (currentIdx < 0)
        newIdx = direction > 0 ? 0 : static_cast<int>(activeSteps.size()) - 1;
    else
    {
        const int count = static_cast<int>(activeSteps.size());
        newIdx = ((currentIdx + direction) % count + count) % count;
    }

    selectedStep_ = activeSteps[static_cast<size_t>(newIdx)];
    refreshPadLedState();
    repaint();
}

void PatternWidget::selectPrevPattern()
{
    if (currentPatternNumber_ <= 0)
        return;

    --currentPatternNumber_;
    auto& sampler = engine().getSampler();
    sampler.setActivePatternIndex(currentPatternNumber_);

    // Reselect the current channel against the new pattern so the edit target
    // (selectedPatternIndex_) refreshes.
    if (selectedChannelPadId_ >= 0)
        selectChannel(selectedChannelPadId_);

    engine().updateLoopRangeForPlayMode();
    selectedStep_ = -1;
    refreshPadLedState();
    repaint();
}

void PatternWidget::selectNextPattern()
{
    auto& sampler = engine().getSampler();
    const int numPatterns = sampler.getNumPatterns();

    if (currentPatternNumber_ + 1 >= numPatterns)
    {
        // Create a new empty pattern that inherits the current bars/stepsPerBar.
        const int newIdx = sampler.createPattern();
        if (newIdx < 0)
            return;
        currentPatternNumber_ = newIdx;
        sampler.setPatternLength(currentPatternNumber_, barsPerPattern_, stepsPerBar_);
    }
    else
    {
        ++currentPatternNumber_;
    }

    sampler.setActivePatternIndex(currentPatternNumber_);

    if (selectedChannelPadId_ >= 0)
        selectChannel(selectedChannelPadId_);

    engine().updateLoopRangeForPlayMode();
    selectedStep_ = -1;
    refreshPadLedState();
    repaint();
}

void PatternWidget::autoSelectFirstChannel()
{
    if (selectedChannelPadId_ >= 0)
        return;

    if (channelMode_ == ChannelMode::Instruments)
    {
        auto& bank = engine().getKeyboardBank();
        if (bank.getNumSlots() > 0)
            selectChannel(0);
        return;
    }

    auto& padBank = engine().getSampler();
    for (const auto& pad : padBank.getPadsSnapshot(
             SamplerInstrument::SnapshotContent::Patterns))
        if (pad.hasSample && ! pad.patterns.empty())
        {
            selectChannel(pad.id);
            return;
        }
}

void PatternWidget::setChannelMode(ChannelMode mode)
{
    if (mode == channelMode_) return;
    channelMode_ = mode;
    // Drop the current selection — autoSelectFirstChannel will pick a
    // row matching the new filter on next refresh.
    selectedChannelPadId_ = -1;
    selectedPatternIndex_ = -1;
    autoSelectFirstChannel();
    refreshPadLedState();
    repaint();
}

void PatternWidget::refreshPadLedState()
{
    auto ownerId = describe().id.toStdString();

    if (!isStepModeActive())
    {
        // Pattern owns the shared pad LEDs after the Pattern button reclaims
        // its resources. In non-Step mode the pads still audition samples, so
        // mirror Pad Overview's populated-pad state instead of publishing an
        // all-off step grid.
        uint8_t populatedColor = HardwareConstants::kColorBlueBright;
        auto& gm = engine().getGroupManager();
        const int activeGroup = gm.getActiveGroupIndex();
        if (activeGroup >= 0)
            populatedColor = HardwareConstants::brightestIndexedVariant(
                gm.getGroupColor(activeGroup));

        const auto snapshots = engine().getSampler().getPadsSnapshot(
            SamplerInstrument::SnapshotContent::State);
        for (int i = 0; i < 16; ++i)
        {
            const bool populated = i < static_cast<int>(snapshots.size())
                                && snapshots[static_cast<size_t>(i)].hasSample;
            hw().setLed(ResourceIds::PadLeds[static_cast<size_t>(i)],
                        populated ? populatedColor : 0,
                        ownerId);
        }
        return;
    }

    // Show step state on pad LEDs
    auto& padBank = engine().getSampler();
    auto snapshots = padBank.getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    const int stepCount = getStepCount();

    // Find the selected channel's pattern
    std::array<uint8_t, 16> colors {};
    for (const auto& pad : snapshots)
    {
        if (pad.id != selectedChannelPadId_
            || static_cast<int>(pad.patterns.size()) <= currentPatternNumber_)
            continue;

        auto& pattern = pad.patterns[static_cast<size_t>(currentPatternNumber_)];
        uint8_t groupColor = HardwareConstants::kColorBlueBright;
        auto& gm = engine().getGroupManager();
        int ag = gm.getActiveGroupIndex();
        if (ag >= 0)
            groupColor = gm.getGroupColor(ag);

        const uint8_t dimColor = HardwareConstants::dimmestIndexedVariant(groupColor);
        const uint8_t brightColor = HardwareConstants::brightestIndexedVariant(groupColor);

        for (int i = 0; i < 16 && i < stepCount; ++i)
        {
            if (!pattern.steps[i])
            {
                colors[static_cast<size_t>(i)] = 0;
                continue;
            }

            const int vel = i < static_cast<int>(pattern.velocities.size())
                                ? pattern.velocities[static_cast<size_t>(i)]
                                : 83;
            colors[static_cast<size_t>(i)] = vel <= 64 ? dimColor : brightColor;
        }

        // Overlay: mark the selected step with a white LED variant
        if (selectedStep_ >= 0 && selectedStep_ < 16)
            colors[static_cast<size_t>(selectedStep_)] = HardwareConstants::kColorWhite;

        break;
    }

    for (int i = 0; i < 16; ++i)
        hw().setLed(ResourceIds::PadLeds[static_cast<size_t>(i)],
                    colors[static_cast<size_t>(i)],
                    ownerId);
}

void PatternWidget::updateStepButtonLed()
{
    auto ownerId = describe().id.toStdString();
    hw().setLed("step",
                isStepModeActive() ? HardwareConstants::kLedBright
                                   : HardwareConstants::kLedDim,
                ownerId);
}

int PatternWidget::getStepCount() const
{
    return barsPerPattern_ * stepsPerBar_;
}

double PatternWidget::getPlayheadStep() const
{
    const double step = engine().getSampler().getPlayheadStep();
    const int cols = juce::jmax(1, getStepCount());

    if (cols > 0)
    {
        double wrapped = std::fmod(step, static_cast<double>(cols));
        if (wrapped < 0.0)
            wrapped += cols;
        return wrapped;
    }

    return juce::jlimit(0.0, static_cast<double>(cols), step);
}

void PatternWidget::recordStepAtPlayhead(int padIndex)
{
    if (!engine().isRecordArmed() || !engine().isPlaying() || engine().isCountingIn())
        return;

    if (selectedPatternIndex_ < 0)
        return;

    const int stepCount = getStepCount();
    if (stepCount <= 0)
        return;

    // Quantize: snap to nearest step
    const double playhead = getPlayheadStep();
    int stepIndex = static_cast<int>(std::round(playhead)) % stepCount;
    if (stepIndex < 0)
        stepIndex += stepCount;

    // Overdub: only set ON (don't toggle)
    auto* edit = engine().getEdit();
    if (edit == nullptr)
        return;

    auto& um = edit->getUndoManager();
    um.perform(new ToggleSequencerStepCommand(engine(), selectedPatternIndex_,
                                               padIndex, stepIndex, true));

    refreshPadLedState();
    repaint();
}

void PatternWidget::eraseSelectedChannel()
{
    if (selectedChannelPadId_ < 0 || selectedPatternIndex_ < 0)
        return;

    auto* edit = engine().getEdit();
    if (edit == nullptr)
        return;

    auto& um = edit->getUndoManager();
    um.beginNewTransaction("Erase Channel");

    const int stepCount = getStepCount();
    for (int i = 0; i < stepCount; ++i)
        engine().getSampler().setStep(selectedPatternIndex_, selectedChannelPadId_, i, false);

    refreshPadLedState();
    repaint();
}

void PatternWidget::beginPatternRename()
{
    auto* wm = windowManager();
    if (wm == nullptr)
        return;
    if (renameField_.isEditing())
        return; // already renaming — ignore re-entries

    // Stash whatever is on the right panel so the T9 widget has a free slot.
    // Popped back on commit / cancel via restoreRenameStashedWidget().
    renameStashedRight_ = wm->takeWidget(DisplaySide::Right);

    const int patternIdx = currentPatternNumber_;

    TextInputComponent::Config cfg;
    cfg.prompt = "Rename pattern";
    cfg.dictionaryScope = "pattern-names";
    cfg.maxLength = 32;
    cfg.charFilter = [](juce::juce_wchar c) {
        return c >= 0x20 && c <= 0x7e;  // printable ASCII
    };
    cfg.initialValue = engine().getSampler().getPatternName(patternIdx);

    renameField_.configure(cfg);
    // Live-paint the in-flight buffer into the titlebar as the user types.
    renameField_.setOnTextChanged([this](juce::String /*live*/) {
        if (auto* w = windowManager())
            w->refreshBars();
        repaint();
    });
    renameField_.setOnCommit([this, patternIdx](juce::String name) {
        engine().getSampler().setPatternName(patternIdx, name);
        restoreRenameStashedWidget();
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

void PatternWidget::restoreRenameStashedWidget()
{
    auto* wm = windowManager();
    if (wm == nullptr)
    {
        renameStashedRight_.reset();
        return;
    }
    if (renameStashedRight_ != nullptr)
        wm->open(std::move(renameStashedRight_), DisplaySide::Right);
}
