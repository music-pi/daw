#include "PianoRollWidget.h"

#include <array>
#include <functional>

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../theme/UiTheme.h"

namespace
{
struct ShiftPadAction
{
    int pad;                                        // 0-based pad index
    void (*invoke)(PianoRollView&, juce::UndoManager&);
};

// Single source of truth for the shift+pad map. Pad indices are 0-based so
// pad 3 on the hardware (bottom row, third from left) is index 2.
// Both the handler (which dispatches) and getShiftPadOverlay (which paints
// the LED highlights) walk this table so they can't drift apart.
const ShiftPadAction kShiftPadTable[] = {
    { 2,  [](PianoRollView&, juce::UndoManager& um) { um.undo(); } },       // pad 3 — step undo
    { 3,  [](PianoRollView&, juce::UndoManager& um) { um.redo(); } },       // pad 4 — step redo
    { 4,  [](PianoRollView& v, juce::UndoManager&) { v.quantise(1.0f); } }, // pad 5
    { 5,  [](PianoRollView& v, juce::UndoManager&) { v.quantise(0.5f); } }, // pad 6
    { 6,  [](PianoRollView& v, juce::UndoManager&) { v.nudgeSelected(-0.25); } }, // pad 7
    { 7,  [](PianoRollView& v, juce::UndoManager&) { v.nudgeSelected(+0.25); } }, // pad 8
    { 12, [](PianoRollView& v, juce::UndoManager&) { v.shiftSelectedPitch(-1); } },  // pad 13
    { 13, [](PianoRollView& v, juce::UndoManager&) { v.shiftSelectedPitch(+1); } },  // pad 14
    { 14, [](PianoRollView& v, juce::UndoManager&) { v.shiftSelectedPitch(-12); } }, // pad 15
    { 15, [](PianoRollView& v, juce::UndoManager&) { v.shiftSelectedPitch(+12); } }, // pad 16
};

constexpr std::array<double, 4> kQuantiseGrids {
    0.5, 0.25, 0.125, 0.0625
};
const std::array<juce::String, 4> kQuantiseGridLabels {
    "1/8", "1/16", "1/32", "1/64"
};
constexpr double kNudgeBeats = 1.0 / 64.0;
constexpr double kFineNudgeBeats = 1.0 / 256.0;
constexpr int kKnobThreshold = 3;
}

void PianoRollWidget::setMidiClip(te::MidiClip* clip, juce::String channelName)
{
    clip_ = clip;
    clipId_ = clip != nullptr ? clip->itemID : te::EditItemID();
    channelName_ = std::move(channelName);
    cachedTitle_ = "Piano Roll > " + channelName_;
    view_.setMidiClip(clip);

    // Auto-range the pitch window to the clip's notes.
    int lo = 36, hi = 60;
    if (clip != nullptr)
    {
        auto notes = clip->getSequence().getNotes();
        if (notes.size() > 0)
        {
            int minP = 127, maxP = 0;
            for (auto* n : notes)
            {
                minP = juce::jmin(minP, n->getNoteNumber());
                maxP = juce::jmax(maxP, n->getNoteNumber());
            }
            lo = juce::jmax(0, minP - 2);
            hi = juce::jmin(127, maxP + 2);
            if (hi - lo < 12) hi = juce::jmin(127, lo + 12);
        }
    }
    view_.setVisiblePitchRange(lo, hi);

    if (clip != nullptr)
    {
        const double lenBeats = clip->getLengthInBeats().inBeats();
        if (lenBeats > 0.0) view_.setVisibleBeats(lenBeats);
    }
}

void PianoRollWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    addAndMakeVisible(view_);
    installInputHandlers();
    resized();
}

void PianoRollWidget::onDeactivated()
{
    releaseInputHandlers();
    clip_ = nullptr;
    view_.setMidiClip(nullptr);
}

void PianoRollWidget::onEditAboutToBeReplaced()
{
    clip_ = nullptr;
    view_.setMidiClip(nullptr);
}

void PianoRollWidget::onEditReplaced()
{
    auto* edit = engine().getEdit();
    if (edit != nullptr && clipId_.isValid())
        clip_ = dynamic_cast<te::MidiClip*>(te::findClipForID(*edit, clipId_));
    view_.setMidiClip(clip_);
    lastPlayheadBeat_ = -1.0;
    repaint();
}

void PianoRollWidget::installInputHandlers()
{
    auto* host = controllerHost();
    if (host == nullptr) return;
    auto* im = host->getInputManager();
    if (im == nullptr) return;

    // View-priority pad handler for shift+pad operations. Swallows the
    // event only when shift is held; plain pad taps pass through to the
    // keyboard-mode handler (which plays the note on the active instrument).
    padBinding_ = im->addPadHandler(
        InputManager::HandlerPriority::Modal, "",
        [this](InputEvent& e) {
            if (! e.isShift()) return;

            // Consume on any shift state — press OR release — so the
            // keyboard-mode handler can't play a note through an unbound
            // shift+pad combo.
            e.consumed = true;
            if (! e.isPressed()) return;

            const int padIdx = e.padIndex();
            if (padIdx < 0 || padIdx >= 16) return;

            auto& um = engine().getUndoManager();
            for (const auto& entry : kShiftPadTable)
                if (entry.pad == padIdx) { entry.invoke(view_, um); break; }
            // Widget::repaint flips the display-dirty flag; PianoRollView's
            // own repaint() would only mark JUCE dirty, not our USB push.
            repaint();
        });

    stepperBinding_ = im->addStepperHandler(
        InputManager::HandlerPriority::Modal, "",
        [this](InputEvent& e) {
            const int dir = e.stepperDirection();
            if (dir == 0) return;
            view_.navigateNotes(dir);
            // Widget::repaint flips the display-dirty flag; PianoRollView's
            // own repaint() would only mark JUCE dirty, not our USB push.
            repaint();
            e.consumed = true;
        });

    // Shift + 4D-encoder tilts move the selected note(s):
    //   shift+navUp/Down    → pitch ±1 semitone
    //   shift+navLeft/Right → nudge ∓0.25 beat
    // Consume only when shift is held so the default nav routing (channel
    // navigation, pattern switch) keeps working unshifted.
    const auto makeTiltHandler = [this](std::function<void(PianoRollView&)> op) {
        return [this, op = std::move(op)](InputEvent& e) {
            if (!e.isPressed() || !e.isShift()) return;
            op(view_);
            repaint();
            e.consumed = true;
        };
    };
    navUpBinding_    = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
        "navUp",    makeTiltHandler([](PianoRollView& v) { v.shiftSelectedPitch(+1); }));
    navDownBinding_  = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
        "navDown",  makeTiltHandler([](PianoRollView& v) { v.shiftSelectedPitch(-1); }));
    navLeftBinding_  = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
        "navLeft",  makeTiltHandler([](PianoRollView& v) { v.nudgeSelected(-0.25); }));
    navRightBinding_ = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
        "navRight", makeTiltHandler([](PianoRollView& v) { v.nudgeSelected(+0.25); }));
}

void PianoRollWidget::releaseInputHandlers()
{
    auto* host = controllerHost();
    if (host == nullptr) return;
    if (auto* im = host->getInputManager())
    {
        for (auto* b : { &padBinding_, &stepperBinding_,
                         &navUpBinding_, &navDownBinding_,
                         &navLeftBinding_, &navRightBinding_ })
        {
            if (*b != kInvalidBinding) im->removeHandler(*b);
            *b = kInvalidBinding;
        }
    }
}

std::unordered_map<int, uint8_t> PianoRollWidget::getShiftPadOverlay(int /*page*/)
{
    std::unordered_map<int, uint8_t> overlay;
    for (const auto& entry : kShiftPadTable)
        overlay.emplace(entry.pad, HardwareConstants::kColorGoldMedium);
    return overlay;
}

std::vector<std::string> PianoRollWidget::requiredResources(int page)
{
    if (page != 0) return {};
    return { "d1", "d2", "d3", "d4" };
}

std::vector<Option> PianoRollWidget::getOptions(int page)
{
    if (page != 0) return {};

    Option close;
    close.id = "pianoroll.close";
    close.label = "Close";
    close.state = OptionState::Enabled;
    close.onInvoke = [this]() { if (onClose_) onClose_(); };

    Option selectAll;
    selectAll.id = "pianoroll.selectAll";
    selectAll.label = view_.allSelected() ? "Deselect All" : "Select All";
    selectAll.state = OptionState::Enabled;
    selectAll.onInvoke = [this]() {
        if (view_.allSelected()) view_.clearSelection();
        else                     view_.selectAll();
    };

    Option quantise;
    quantise.id = "pianoroll.quantise";
    quantise.label = "Quantize " + juce::String(quantiseStrengthPercent_) + "%";
    quantise.state = view_.hasSelection() ? OptionState::Enabled
                                          : OptionState::Disabled;
    quantise.onInvoke = [this]() { applyQuantise(); };

    Option deleteOpt;
    deleteOpt.id = "pianoroll.delete";
    deleteOpt.label = "Delete";
    deleteOpt.state = view_.hasSelection() ? OptionState::Enabled
                                           : OptionState::Disabled;
    deleteOpt.onInvoke = [this]() {
        view_.deleteSelected();
        // Widget-level repaint so the display flips dirty (view_ is a bare
        // juce::Component — its own repaint doesn't push to hardware).
        repaint();
    };

    return { close, selectAll, quantise, deleteOpt };
}

std::vector<Knob> PianoRollWidget::getKnobs(int page)
{
    if (page != 0) return {};

    Knob strength;
    strength.id = "pianoroll.quantiseStrength";
    strength.label = "Quantize";
    strength.isEnabled = true;
    strength.continuousMode = true;
    strength.model = Knob::NumericModel {
        .value = static_cast<double>(quantiseStrengthPercent_),
        .minimum = 0.0,
        .maximum = 100.0,
        .step = 5.0,
        .formatter = [](double value) {
            return juce::String(juce::roundToInt(value)) + "%";
        }
    };

    Knob nudge;
    nudge.id = "pianoroll.nudge";
    nudge.label = "Nudge";
    nudge.isEnabled = view_.hasSelection();
    nudge.continuousMode = true;
    nudge.model = Knob::NumericModel {
        .value = kNudgeBeats,
        .minimum = -1.0,
        .maximum = 1.0,
        .step = kNudgeBeats,
        .formatter = [](double) { return juce::String("±1/64"); }
    };

    Knob grid;
    grid.id = "pianoroll.quantiseGrid";
    grid.label = "Grid";
    grid.isEnabled = true;
    grid.model = Knob::ListModel {
        .entries = std::vector<juce::String>(
            kQuantiseGridLabels.begin(), kQuantiseGridLabels.end()),
        .selectedIndex = quantiseGridIndex_
    };

    Knob empty;
    empty.id = "pianoroll.empty";
    empty.isEnabled = false;
    return { strength, nudge, grid, empty };
}

void PianoRollWidget::handleKnob(int localIndex, int16_t delta, uint16_t,
                                 bool shift)
{
    if (localIndex < 0 || localIndex >= 3 || delta == 0)
        return;

    auto& accumulator = knobAccumulator_[static_cast<size_t>(localIndex)];
    accumulator += delta;
    if (std::abs(accumulator) < kKnobThreshold)
        return;
    const int direction = accumulator > 0 ? 1 : -1;
    accumulator = 0;

    if (localIndex == 0)
    {
        const int increment = shift ? 1 : 5;
        quantiseStrengthPercent_ = juce::jlimit(
            0, 100, quantiseStrengthPercent_ + direction * increment);
    }
    else if (localIndex == 1)
    {
        view_.nudgeSelected(direction * (shift ? kFineNudgeBeats
                                              : kNudgeBeats));
    }
    else if (localIndex == 2)
    {
        quantiseGridIndex_ = juce::jlimit(
            0, static_cast<int>(kQuantiseGrids.size()) - 1,
            quantiseGridIndex_ + direction);
    }
    repaint();
}

void PianoRollWidget::applyQuantise()
{
    view_.quantise(static_cast<float>(quantiseStrengthPercent_) / 100.0f,
                   quantiseGridBeats());
    repaint();
}

double PianoRollWidget::quantiseGridBeats() const
{
    return kQuantiseGrids[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(kQuantiseGrids.size()) - 1,
        quantiseGridIndex_))];
}

void PianoRollWidget::onUiHostTick()
{
    // Drive the playhead off the edit's transport. The widget's base has
    // an AudioEngine pointer and we compute beats-within-clip to match the
    // view_'s coordinate system.
    const auto updatePlayhead = [this](double beat)
    {
        if (beat == lastPlayheadBeat_)
            return;
        lastPlayheadBeat_ = beat;
        view_.setPlayheadBeat(beat);
        // PianoRollView is a leaf Component, so its repaint alone does not
        // reach UiHost's hardware-display dirty gate.
        repaint();
    };

    if (clip_ == nullptr) { updatePlayhead(-1.0); return; }
    if (! hasHardware()) { updatePlayhead(-1.0); return; }

    auto* edit = engine().getEdit();
    if (edit == nullptr || ! engine().isPlaying())
    {
        updatePlayhead(-1.0);
        return;
    }

    const auto tpos = edit->getTransport().getPosition();
    const auto beatsAbs = edit->tempoSequence.toBeats(tpos);
    const auto clipStart = edit->tempoSequence.toBeats(clip_->getPosition().getStart());
    const double beatInClip = (beatsAbs - clipStart).inBeats();
    updatePlayhead(beatInClip);
}

void PianoRollWidget::paint(juce::Graphics& g)
{
    paintPage(g, 0, getLocalBounds());
}

void PianoRollWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (page != 0) return;
    g.fillAll(UiTheme::kBackgroundDark);
}

void PianoRollWidget::resized()
{
    view_.setBounds(getLocalBounds());
}
