#include "ArrangerWidget.h"

#include <cmath>

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/SamplerInstrument.h"
#include "../hw/HardwareState.h"
#include "../theme/UiTheme.h"
#include "WindowManager.h"

namespace
{
// Sensitivity carried over from the single-lane arranger. The 4D encoder is
// twitchy on a 1-bar-per-step knob; 0.02 ≈ 50 notches per step feels right.
constexpr double kKnobSensitivity = 0.02;

juce::Colour dimmedAccent(juce::Colour base)
{
    return base.withAlpha(0.35f);
}
} // namespace

ArrangerWidget::ArrangerWidget()
{
    // The rename field is a T9 driver, not a rendered component. Matching
    // PatternWidget's pattern: keep it as an invisible child so
    // findEnclosingWidget() resolves to us, but never addAndMakeVisible.
    addChildComponent(renameField_);
}

ArrangerWidget::~ArrangerWidget() = default;

// ── Widget identity ──────────────────────────────────────────────────────

WidgetDescriptor ArrangerWidget::describe() const
{
    return { "arranger", 1, false, DisplayConstraint::LeftOnly };
}

// ── Lifecycle ────────────────────────────────────────────────────────────

void ArrangerWidget::onActivated(int offset)
{
    panelOffset_ = offset;
    transportSnapshot_ = engine().getTransportSnapshot();
    clampCursor();

    hw().setLed("arranger", HardwareConstants::kLedBright,
                describe().id.toStdString());

    // 4D encoder turn = cursor bar ±1. Registered on activate, torn down on
    // deactivate so it doesn't leak when the widget is swapped out.
    if (auto* host = controllerHost())
    {
        if (auto* im = host->getInputManager())
        {
            stepperBinding_ = im->addStepperHandler(
                InputManager::HandlerPriority::View, "",
                [this](InputEvent& e) {
                    const int dir = e.stepperDirection();
                    if (dir == 0) return;
                    moveCursor(dir, 0);
                    e.consumed = true;
                });

            // 4D encoder tilts move the cursor: up/down = lane ±1,
            // left/right = bar ±1 (mirrors the stepper path). Unshifted so
            // shift stays free for the duplicateDouble = "double" flow.
            const auto makeTilt = [this](int dBar, int dLane) {
                return [this, dBar, dLane](InputEvent& e) {
                    if (!e.isPressed()) return;
                    moveCursor(dBar, dLane);
                    e.consumed = true;
                };
            };
            navUpBinding_    = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
                "navUp",    makeTilt(0, -1));
            navDownBinding_  = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
                "navDown",  makeTilt(0, +1));
            navLeftBinding_  = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
                "navLeft",  makeTilt(-1, 0));
            navRightBinding_ = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
                "navRight", makeTilt(+1, 0));

            // duplicateDouble button: unshifted = unique (clone pattern in
            // place), shifted = double (clone + insert a second block after
            // the source). Replaces the old d3 "Unique" slot.
            duplicateBinding_ = im->addButtonHandler(InputManager::HandlerPriority::Modal, "",
                "duplicateDouble",
                [this](InputEvent& e) {
                    if (!e.isPressed()) return;
                    if (cursorBlockIndex() < 0)
                    {
                        e.consumed = true;
                        return;
                    }
                    if (e.isShift())
                        doubleBlockAtCursor();
                    else
                        uniqueBlockAtCursor();
                    e.consumed = true;
                });
        }
    }
}

void ArrangerWidget::onDeactivated()
{
    if (auto* host = controllerHost())
    {
        if (auto* im = host->getInputManager())
        {
            for (auto* b : { &stepperBinding_,
                             &navUpBinding_, &navDownBinding_,
                             &navLeftBinding_, &navRightBinding_,
                             &duplicateBinding_ })
            {
                if (*b != kInvalidBinding) im->removeHandler(*b);
                *b = kInvalidBinding;
            }
        }
    }

    // Clear rename callbacks before ending edit — cancel would pop the stashed
    // widget back into a panel we no longer own. Drop the stash outright.
    renameField_.setOnCommit(nullptr);
    renameField_.setOnCancel(nullptr);
    renameField_.setOnTextChanged(nullptr);
    if (renameField_.isEditing())
        renameField_.endEdit(false);
    renameStashedOpposite_.reset();
}

// ── Resources ────────────────────────────────────────────────────────────

std::vector<std::string> ArrangerWidget::requiredResources(int page)
{
    if (page != 0)
        return {};

    std::vector<std::string> resources;
    resources.push_back("arranger");

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

// ── Options & Knobs ──────────────────────────────────────────────────────

std::vector<Option> ArrangerWidget::getOptions(int page)
{
    if (page != 0)
        return {};
    const bool shift = controllerHost() != nullptr && controllerHost()->isShiftPressed();
    return buildOptions(shift);
}

std::vector<Option> ArrangerWidget::getOptionsForShiftState(bool shift)
{
    return buildOptions(shift);
}

std::vector<Option> ArrangerWidget::buildOptions(bool shift)
{
    auto& sampler = engine().getSampler();
    const int blockIdx = cursorBlockIndex();
    const bool hasBlock = blockIdx >= 0;

    Option d1;
    Option d2;
    Option d3;
    Option d4;

    if (!shift)
    {
        // ── Non-shift: Insert / Delete / (empty) / Mode ──
        // Unique moved off d3 onto the hardware duplicateDouble button
        // (unshifted = unique, shifted = double).
        d1.id = "arranger.insert";
        d1.label = "Insert";
        d1.state = OptionState::Enabled;
        d1.onInvoke = [this]() { insertBlockAtCursor(); };

        d2.id = "arranger.delete";
        d2.label = "Delete";
        d2.state = hasBlock ? OptionState::Enabled : OptionState::Disabled;
        d2.onInvoke = [this]() { deleteBlockAtCursor(); };

        d3.id = "arranger.empty";
        d3.label = "";
        d3.state = OptionState::Empty;
    }
    else
    {
        // ── Shift: Rename / Add Lane / Remove Lane / Mode ──
        d1.id = "arranger.rename";
        d1.label = "Rename";
        d1.state = hasBlock ? OptionState::Enabled : OptionState::Disabled;
        d1.onInvoke = [this]() { beginBlockRename(); };

        d2.id = "arranger.addLane";
        d2.label = "Add Lane";
        d2.state = OptionState::Enabled;
        d2.onInvoke = [this]() { addLaneAfterCursor(); };

        d3.id = "arranger.removeLane";
        d3.label = "Rm Lane";
        const bool canRemove = sampler.getNumSongLanes() > 1;
        d3.state = canRemove ? OptionState::Enabled : OptionState::Disabled;
        d3.onInvoke = [this]() { removeCursorLane(); };
    }

    // d4: Mode toggle — always visible regardless of shift.
    d4.id = "arranger.mode";
    const bool trackMode = engine().getPlayMode() == AudioEngine::PlayMode::Track;
    d4.label = trackMode ? "Mode: Track" : "Mode: Pattern";
    d4.state = OptionState::Enabled;
    d4.onInvoke = [this]() { togglePlayMode(); };

    return { std::move(d1), std::move(d2), std::move(d3), std::move(d4) };
}

std::vector<Knob> ArrangerWidget::getKnobs(int page)
{
    if (page != 0)
        return {};

    auto& sampler = engine().getSampler();
    const auto& lanes = sampler.getSongLanes();
    const int laneSafe = juce::jlimit(0, juce::jmax(0, (int) lanes.size() - 1), cursorLane_);
    const int blockIdx = cursorBlockIndex();
    const bool hasBlock = blockIdx >= 0;

    const SamplerInstrument::SongBlock* block = nullptr;
    if (hasBlock && laneSafe < (int) lanes.size()
        && blockIdx < (int) lanes[(size_t) laneSafe].blocks.size())
        block = &lanes[(size_t) laneSafe].blocks[(size_t) blockIdx];

    // K1 — pattern selector for the block under the cursor.
    Knob k1;
    k1.id = "arranger.pattern";
    k1.label = "Pattern";
    k1.isEnabled = hasBlock;
    k1.sensitivity = kKnobSensitivity;
    k1.shiftSensitivity = 1.0;
    {
        Knob::ListModel lm;
        const int nPatterns = juce::jmax(1, sampler.getNumPatterns());
        lm.entries.reserve((size_t) nPatterns);
        for (int i = 0; i < nPatterns; ++i)
            lm.entries.push_back(sampler.getPatternName(i));
        lm.selectedIndex = hasBlock ? juce::jlimit(0, nPatterns - 1, block->patternIndex) : 0;
        lm.onChange = [this, laneSafe](int newIndex) {
            const int idx = cursorBlockIndex();
            if (idx < 0) return;
            engine().getSampler().setBlockPattern(laneSafe, idx, newIndex);
            if (auto* wm = windowManager()) wm->refreshBars();
            repaint();
        };
        k1.model = std::move(lm);
    }

    // K2 — bars for the cursor block.
    Knob k2;
    k2.id = "arranger.bars";
    k2.label = "Bars";
    k2.isEnabled = hasBlock;
    k2.sensitivity = kKnobSensitivity;
    k2.shiftSensitivity = 1.0;
    {
        Knob::NumericModel nm;
        nm.value = hasBlock ? (double) block->bars : 1.0;
        nm.minimum = 1.0;
        nm.maximum = 64.0;
        nm.step = 1.0;
        nm.formatter = [](double v) { return juce::String((int) v); };
        nm.onChange = [this, laneSafe](double v) {
            const int idx = cursorBlockIndex();
            if (idx < 0) return;
            const bool ok = engine().getSampler().setBlockBars(laneSafe, idx, (int) v);
            if (!ok)
                showToast(ToastKind::Warning, "Overlap");
            if (auto* wm = windowManager()) wm->refreshBars();
            repaint();
        };
        k2.model = std::move(nm);
    }

    // K3 — start bar for the cursor block.
    Knob k3;
    k3.id = "arranger.start";
    k3.label = "Start";
    k3.isEnabled = hasBlock;
    k3.sensitivity = kKnobSensitivity;
    k3.shiftSensitivity = 1.0;
    {
        Knob::NumericModel nm;
        nm.value = hasBlock ? (double) block->startBar : 0.0;
        nm.minimum = 0.0;
        nm.maximum = (double) (sampler.getSongTotalBars() + 32);
        nm.step = 1.0;
        nm.formatter = [](double v) { return juce::String((int) v); };
        nm.onChange = [this, laneSafe](double v) {
            const int idx = cursorBlockIndex();
            if (idx < 0) return;
            const bool ok = engine().getSampler().setBlockStart(laneSafe, idx, (int) v);
            if (!ok)
                showToast(ToastKind::Warning, "Overlap");
            if (auto* wm = windowManager()) wm->refreshBars();
            repaint();
        };
        k3.model = std::move(nm);
    }

    // K4 — cursor lane. Numeric 0..numLanes-1.
    Knob k4;
    k4.id = "arranger.lane";
    k4.label = "Lane";
    const int numLanes = juce::jmax(1, sampler.getNumSongLanes());
    k4.isEnabled = numLanes > 1;
    k4.sensitivity = kKnobSensitivity;
    k4.shiftSensitivity = 1.0;
    {
        Knob::NumericModel nm;
        nm.value = (double) juce::jlimit(0, numLanes - 1, cursorLane_);
        nm.minimum = 0.0;
        nm.maximum = (double) (numLanes - 1);
        nm.step = 1.0;
        nm.formatter = [](double v) { return juce::String((int) v + 1); };
        nm.onChange = [this](double v) {
            cursorLane_ = juce::jmax(0, (int) v);
            clampCursor();
            if (auto* wm = windowManager()) wm->refreshBars();
            repaint();
        };
        k4.model = std::move(nm);
    }

    return { std::move(k1), std::move(k2), std::move(k3), std::move(k4) };
}

// ── Rendering ────────────────────────────────────────────────────────────

void ArrangerWidget::paint(juce::Graphics& g)
{
    g.fillAll(UiTheme::kBackgroundDark);
    paintPage(g, 0, getLocalBounds());
}

void ArrangerWidget::paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds)
{
    if (page != 0)
        return;

    auto content = bounds.reduced(8);
    ensureCursorVisible(content);

    auto& sampler = engine().getSampler();
    const auto& lanes = sampler.getSongLanes();
    const int numLanes = (int) lanes.size();

    // Gutter on the left reserved for "Lane N" labels; the grid occupies the
    // remaining width.
    const int gridX = content.getX() + kLaneLabelWidth;
    const int gridW = juce::jmax(0, content.getRight() - gridX);
    const int visibleBars = juce::jmax(1, gridW / kBarWidthPx);

    // ── Ruler ──
    auto rulerArea = juce::Rectangle<int>(gridX, content.getY(), gridW, kRulerHeightPx);
    g.setColour(UiTheme::kTitlebarBackground);
    g.fillRect(rulerArea);

    g.setColour(UiTheme::kTextSecondary);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));

    for (int i = 0; i <= visibleBars; ++i)
    {
        const int bar = viewStartBar_ + i;
        const int x = gridX + i * kBarWidthPx;
        if (x > content.getRight()) break;

        const bool majorTick = (bar % 4) == 0;
        const int tickH = majorTick ? 6 : 3;
        g.fillRect(x, rulerArea.getBottom() - tickH, 1, tickH);
        if (majorTick)
        {
            juce::Rectangle<int> label(x + 1, rulerArea.getY(), kBarWidthPx * 4 - 2, kRulerHeightPx - tickH);
            g.drawText(juce::String(bar + 1), label,
                       juce::Justification::bottomLeft, false);
        }
    }

    // Highlight the cursor's current bar in the ruler.
    {
        const int cx = gridX + (cursorBar_ - viewStartBar_) * kBarWidthPx;
        if (cx >= gridX && cx < rulerArea.getRight())
        {
            g.setColour(UiTheme::kTitlebarAccent.withAlpha(0.5f));
            g.fillRect(cx, rulerArea.getY(), kBarWidthPx, rulerArea.getHeight());
        }
    }

    // ── Lane rows ──
    const int rowsAreaTop = content.getY() + kRulerHeightPx + 4;
    const int rowsAreaH = juce::jmax(0, content.getBottom() - rowsAreaTop);
    const int maxVisibleLanes = juce::jmax(1, (rowsAreaH + kLaneGapPx) / (kLaneHeightPx + kLaneGapPx));

    // Simple vertical scroll: if the cursor lane would fall outside the
    // visible window, start from the cursor lane. Otherwise start at 0.
    int firstLane = 0;
    if (cursorLane_ >= maxVisibleLanes)
        firstLane = cursorLane_ - maxVisibleLanes + 1;

    const juce::Colour laneBg     = UiTheme::kTitlebarBackground.darker(0.4f);
    const juce::Colour laneBorder = UiTheme::kTitlebarBackground.brighter(0.1f);
    const juce::Colour accent     = UiTheme::kTitlebarAccent;
    const juce::Colour accentDim  = dimmedAccent(accent);

    for (int li = 0; li < maxVisibleLanes; ++li)
    {
        const int laneIdx = firstLane + li;
        if (laneIdx >= numLanes) break;

        const int y = rowsAreaTop + li * (kLaneHeightPx + kLaneGapPx);

        // Lane gutter — "Lane N" label.
        juce::Rectangle<int> gutter(content.getX(), y, kLaneLabelWidth, kLaneHeightPx);
        g.setColour(laneBg);
        g.fillRect(gutter);
        g.setColour(laneIdx == cursorLane_ ? juce::Colours::white : UiTheme::kTextSecondary);
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel, juce::Font::bold)));
        g.drawText(juce::String("Lane ") + juce::String(laneIdx + 1),
                   gutter.reduced(4, 0),
                   juce::Justification::centredLeft, false);

        // Lane background stripe.
        juce::Rectangle<int> laneRect(gridX, y, gridW, kLaneHeightPx);
        g.setColour(laneBg);
        g.fillRect(laneRect);
        g.setColour(laneBorder);
        g.drawRect(laneRect, 1);

        // Blocks in this lane.
        const auto& blocks = lanes[(size_t) laneIdx].blocks;
        const int focusedBlockIdx = (laneIdx == cursorLane_) ? cursorBlockIndex() : -1;

        for (size_t bi = 0; bi < blocks.size(); ++bi)
        {
            const auto& b = blocks[bi];
            const int blockX = gridX + (b.startBar - viewStartBar_) * kBarWidthPx;
            const int blockW = b.bars * kBarWidthPx;

            // Skip blocks entirely off-screen.
            if (blockX + blockW <= gridX || blockX >= gridX + gridW)
                continue;

            // Clip to the visible grid.
            juce::Rectangle<int> rect(blockX, y + 1, blockW, kLaneHeightPx - 2);
            rect = rect.getIntersection(juce::Rectangle<int>(gridX, y + 1, gridW, kLaneHeightPx - 2));
            if (rect.isEmpty()) continue;

            const bool focused = ((int) bi == focusedBlockIdx);
            g.setColour(focused ? accent : accentDim);
            g.fillRect(rect);

            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawRect(rect, 1);

            // Labels: pattern name (or live rename buffer) + "×N".
            const bool renaming = focused && renameField_.isEditing();
            juce::String nameText;
            if (renaming)
            {
                nameText = renameField_.getText()
                         + juce::String::charToString((juce::juce_wchar) 0x2588);
            }
            else
            {
                nameText = sampler.getPatternName(b.patternIndex);
            }

            g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel, juce::Font::bold)));
            auto text = rect.reduced(3, 1);
            auto top = text.removeFromTop(text.getHeight() / 2 + 1);
            g.setColour(renaming ? UiTheme::kTitlebarAccent : juce::Colours::black);
            g.drawText(nameText, top, juce::Justification::centredLeft, false);

            g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kSmallLabel)));
            g.setColour(juce::Colours::black.withAlpha(0.8f));
            g.drawText(juce::String::charToString((juce::juce_wchar) 0x00D7) + juce::String(b.bars),
                       text, juce::Justification::centredLeft, false);
        }

        // ── Ghost cursor on empty cell ──
        if (laneIdx == cursorLane_ && focusedBlockIdx < 0)
        {
            const int cx = gridX + (cursorBar_ - viewStartBar_) * kBarWidthPx;
            if (cx >= gridX && cx < gridX + gridW)
            {
                juce::Rectangle<int> ghost(cx, y + 1, kBarWidthPx, kLaneHeightPx - 2);
                ghost = ghost.getIntersection(juce::Rectangle<int>(gridX, y + 1, gridW, kLaneHeightPx - 2));
                if (!ghost.isEmpty())
                {
                    g.setColour(UiTheme::kTitlebarAccent.withAlpha(0.35f));
                    g.drawRect(ghost, 1);
                }
            }
        }
    }

    // ── Playhead ──
    if (playheadBar_ >= 0)
    {
        const int px = gridX + (playheadBar_ - viewStartBar_) * kBarWidthPx;
        if (px >= gridX && px < gridX + gridW)
        {
            g.setColour(juce::Colour(0xffee3b3b));
            const int top = rowsAreaTop;
            const int bottom = content.getBottom();
            g.fillRect(px, top, 1, bottom - top);
        }
    }
}

// ── Input ────────────────────────────────────────────────────────────────

void ArrangerWidget::handleKnob(int localIndex, int16_t delta, uint16_t /*absolute*/, bool shift)
{
    auto knobs = getKnobs(0);
    if (localIndex < 0 || localIndex >= (int) knobs.size()) return;
    auto& knob = knobs[(size_t) localIndex];
    if (!knob.isEnabled) return;

    const double scale = shift ? knob.shiftSensitivity : knob.sensitivity;

    if (auto* nm = std::get_if<Knob::NumericModel>(&knob.model))
    {
        // Accumulate fractional contributions across calls — at sensitivity
        // 0.02 and step 1.0, one tick contributes 0.02. Only fire onChange
        // when the accumulator has moved the value across an integer step.
        auto& acc = knobAccumulator_[(size_t) localIndex];
        acc += (double) delta * nm->step * scale;

        const double target = juce::jlimit(nm->minimum, nm->maximum, nm->value + acc);
        const int snapped = (int) std::round(target);
        if (snapped != (int) nm->value)
        {
            // Consume the integer portion; leftover fraction carries to the
            // next call so a slow spin doesn't lose precision.
            acc -= (double) snapped - nm->value;
            if (nm->onChange) nm->onChange((double) snapped);
        }
    }
    else if (auto* lm = std::get_if<Knob::ListModel>(&knob.model))
    {
        const int total = (int) lm->entries.size();
        if (total <= 0) return;
        // List knobs also accumulate — same feel at scale=0.02 → ~50 ticks
        // per entry instead of one-entry-per-tick whiplash.
        auto& acc = knobAccumulator_[(size_t) localIndex];
        acc += (double) delta * scale;
        if (std::abs(acc) >= 1.0)
        {
            const int dir = acc > 0 ? 1 : -1;
            acc -= (double) dir;
            const int newIndex = juce::jlimit(0, total - 1, lm->selectedIndex + dir);
            if (newIndex != lm->selectedIndex && lm->onChange)
                lm->onChange(newIndex);
        }
    }
}

void ArrangerWidget::handleOption(int localIndex)
{
    auto opts = getOptions(0);
    if (localIndex < 0 || localIndex >= (int) opts.size()) return;
    auto& o = opts[(size_t) localIndex];
    if (o.state == OptionState::Disabled || o.state == OptionState::Empty) return;
    if (o.onInvoke) o.onInvoke();
}

void ArrangerWidget::onUiHostTick()
{
    transportSnapshot_ = engine().getTransportSnapshot();

    const bool hadPlayhead = playheadBar_ >= 0;
    int newBar = -1;

    if (engine().getPlayMode() == AudioEngine::PlayMode::Track
        && transportSnapshot_.isPlaying)
    {
        if (auto* edit = engine().getEdit())
        {
            const double seconds = transportSnapshot_.positionSeconds;
            const auto beats = edit->tempoSequence.toBeats(
                tracktion::core::TimePosition::fromSeconds(juce::jmax(0.0, seconds)));
            double beatsPerBar = 4.0;
            if (auto* ts = edit->tempoSequence.getTimeSig(0))
                beatsPerBar = juce::jmax(1, ts->numerator.get());
            newBar = (int) std::floor(beats.inBeats() / beatsPerBar);
        }
    }

    if (newBar != playheadBar_)
    {
        playheadBar_ = newBar;
        repaint();
    }
    else if (hadPlayhead && playheadBar_ < 0)
    {
        repaint();
    }
}

// ── Public actions ───────────────────────────────────────────────────────

void ArrangerWidget::insertBlockAtCursor()
{
    auto& sampler = engine().getSampler();
    const int inserted = sampler.insertBlock(cursorLane_, cursorBar_, /*pattern*/ 0, /*bars*/ 1);
    if (inserted < 0)
    {
        showToast(ToastKind::Warning, "Occupied");
        return;
    }
    if (auto* wm = windowManager()) wm->refreshBars();
    repaint();
}

void ArrangerWidget::deleteBlockAtCursor()
{
    const int idx = cursorBlockIndex();
    if (idx < 0) return;
    engine().getSampler().removeBlock(cursorLane_, idx);
    if (auto* wm = windowManager()) wm->refreshBars();
    repaint();
}

void ArrangerWidget::uniqueBlockAtCursor()
{
    const int idx = cursorBlockIndex();
    if (idx < 0) return;
    const int newPattern = engine().getSampler().uniqueSongBlock(cursorLane_, idx);
    if (newPattern < 0) return;
    if (auto* wm = windowManager()) wm->refreshBars();
    repaint();
    showToast(ToastKind::Success, "Unique pattern");
}

void ArrangerWidget::doubleBlockAtCursor()
{
    const int idx = cursorBlockIndex();
    if (idx < 0) return;
    const int newBlockIdx = engine().getSampler().doubleSongBlock(cursorLane_, idx);
    if (auto* wm = windowManager()) wm->refreshBars();
    repaint();
    if (newBlockIdx < 0)
        showToast(ToastKind::Warning, "Overlap");
    else
        showToast(ToastKind::Success, "Doubled");
}

void ArrangerWidget::togglePlayMode()
{
    const auto current = engine().getPlayMode();
    engine().setPlayMode(current == AudioEngine::PlayMode::Pattern
                             ? AudioEngine::PlayMode::Track
                             : AudioEngine::PlayMode::Pattern);
    if (auto* wm = windowManager()) wm->refreshBars();
    repaint();
}

void ArrangerWidget::addLaneAfterCursor()
{
    engine().getSampler().insertLane(cursorLane_ + 1);
    if (auto* wm = windowManager()) wm->refreshBars();
    repaint();
}

void ArrangerWidget::removeCursorLane()
{
    auto& sampler = engine().getSampler();
    if (sampler.getNumSongLanes() <= 1)
    {
        showToast(ToastKind::Warning, "Last lane");
        return;
    }
    sampler.removeLane(cursorLane_);
    clampCursor();
    if (auto* wm = windowManager()) wm->refreshBars();
    repaint();
}

void ArrangerWidget::setCursorForTest(int lane, int bar)
{
    cursorLane_ = juce::jmax(0, lane);
    cursorBar_ = juce::jmax(0, bar);
    clampCursor();
    repaint();
}

int ArrangerWidget::cursorBlockIndex() const
{
    const auto& sampler = engine().getSampler();
    const auto& lanes = sampler.getSongLanes();
    if (cursorLane_ < 0 || cursorLane_ >= (int) lanes.size()) return -1;

    const auto& blocks = lanes[(size_t) cursorLane_].blocks;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& b = blocks[i];
        if (cursorBar_ >= b.startBar && cursorBar_ < b.startBar + b.bars)
            return (int) i;
    }
    return -1;
}

// ── Rename flow ──────────────────────────────────────────────────────────

void ArrangerWidget::beginBlockRename()
{
    auto* wm = windowManager();
    if (wm == nullptr) return;
    if (renameField_.isEditing()) return; // re-entrancy guard

    auto& sampler = engine().getSampler();
    const int idx = cursorBlockIndex();
    if (idx < 0) return;

    const auto& lanes = sampler.getSongLanes();
    if (cursorLane_ >= (int) lanes.size()) return;
    if (idx >= (int) lanes[(size_t) cursorLane_].blocks.size()) return;

    const int patternIdx = lanes[(size_t) cursorLane_].blocks[(size_t) idx].patternIndex;

    // Stash the opposite panel so the T9 widget has a free slot to drop onto.
    const auto ourSide = wm->getSide(this);
    const auto oppositeSide = (ourSide == DisplaySide::Left) ? DisplaySide::Right : DisplaySide::Left;
    renameStashedOpposite_ = wm->takeWidget(oppositeSide);

    TextInputComponent::Config cfg;
    cfg.prompt = "Rename pattern";
    cfg.dictionaryScope = "pattern-names";
    cfg.maxLength = 32;
    cfg.charFilter = [](juce::juce_wchar c) {
        return c >= 0x20 && c <= 0x7e; // printable ASCII
    };
    cfg.initialValue = sampler.getPatternName(patternIdx);

    renameField_.configure(cfg);
    renameField_.setOnTextChanged([this](juce::String /*live*/) {
        if (auto* w = windowManager()) w->refreshBars();
        repaint();
    });
    renameField_.setOnCommit([this, patternIdx](juce::String name) {
        engine().getSampler().setPatternName(patternIdx, name);
        restoreRenameStashedWidget();
        if (auto* w = windowManager()) w->refreshBars();
        repaint();
        showToast(ToastKind::Success, "Renamed");
    });
    renameField_.setOnCancel([this]() {
        restoreRenameStashedWidget();
    });
    renameField_.beginEdit();
}

void ArrangerWidget::restoreRenameStashedWidget()
{
    auto* wm = windowManager();
    if (wm == nullptr)
    {
        renameStashedOpposite_.reset();
        return;
    }
    if (renameStashedOpposite_ != nullptr)
    {
        const auto ourSide = wm->getSide(this);
        const auto oppositeSide = (ourSide == DisplaySide::Left) ? DisplaySide::Right : DisplaySide::Left;
        wm->open(std::move(renameStashedOpposite_), oppositeSide);
    }
}

// ── Internals ────────────────────────────────────────────────────────────

void ArrangerWidget::moveCursor(int deltaBar, int deltaLane)
{
    if (deltaBar == 0 && deltaLane == 0)
        return;
    if (deltaBar != 0)
        cursorBar_ = juce::jmax(0, cursorBar_ + deltaBar);
    if (deltaLane != 0)
        cursorLane_ = cursorLane_ + deltaLane; // clamped below
    clampCursor();
    repaint();
    if (auto* wm = windowManager())
        wm->refreshBars();
}

void ArrangerWidget::clampCursor()
{
    const int numLanes = juce::jmax(1, engine().getSampler().getNumSongLanes());
    cursorLane_ = juce::jlimit(0, numLanes - 1, cursorLane_);
    cursorBar_ = juce::jmax(0, cursorBar_);
    viewStartBar_ = juce::jmax(0, viewStartBar_);
}

void ArrangerWidget::ensureCursorVisible(juce::Rectangle<int> content)
{
    const int gridW = juce::jmax(0, content.getRight() - content.getX() - kLaneLabelWidth);
    const int visibleBars = juce::jmax(1, gridW / kBarWidthPx);

    if (cursorBar_ < viewStartBar_)
        viewStartBar_ = cursorBar_;
    else if (cursorBar_ >= viewStartBar_ + visibleBars)
        viewStartBar_ = cursorBar_ - visibleBars + 1;
    viewStartBar_ = juce::jmax(0, viewStartBar_);
}
