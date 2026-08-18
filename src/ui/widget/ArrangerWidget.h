#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../engine/AudioEngine.h"
#include "../../input/InputManager.h"
#include "../components/TextInputComponent.h"
#include "Widget.h"

/**
 * ArrangerWidget — multi-lane song grid.
 *
 * Renders the SamplerInstrument's SongLane/SongBlock model as an
 * FL-studio style horizontal grid: lanes stacked vertically, blocks
 * drawn at proportional widths from (startBar, bars). A cursor moves
 * per (lane, bar); knobs edit the block under the cursor; options
 * insert/delete/unique blocks and (under shift) manage lanes + rename
 * the pattern under the cursor block.
 *
 * Descriptor: id="arranger", pageCount=1, display=LeftOnly.
 */
class ArrangerWidget : public Widget
{
public:
    ArrangerWidget();
    ~ArrangerWidget() override;

    // ── Widget identity ──
    WidgetDescriptor describe() const override;
    juce::String getTitle() const override { return "Arranger"; }

    // ── Lifecycle ──
    void onActivated(int panelOffset) override;
    void onDeactivated() override;

    // ── Resources ──
    std::vector<std::string> requiredResources(int page) override;

    // ── Options & Knobs ──
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    // ── Rendering ──
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;

    // ── Input ──
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;
    void handleOption(int localIndex) override;
    void onUiHostTick() override;

    // ── Test hooks ──
    int getCursorLane() const { return cursorLane_; }
    int getCursorBar() const { return cursorBar_; }
    int getPlayheadBar() const { return playheadBar_; }
    void setCursorForTest(int lane, int bar);

    /** Public wrappers around the d1/d2/d3/d4 actions so tests don't need to
        poke through handleOption + the option-list ordering. */
    void insertBlockAtCursor();
    void deleteBlockAtCursor();
    void uniqueBlockAtCursor();
    void doubleBlockAtCursor();
    void togglePlayMode();

    /** Shift-layout actions — called directly by tests so they don't need to
        fake a held shift modifier on the ControllerHost. */
    void addLaneAfterCursor();
    void removeCursorLane();
    void beginBlockRename();

    /** Test helper: returns the option list as it would appear with the given
        shift state. Lets tests verify the shift layout without driving the
        mock controller through a button press sequence. */
    std::vector<Option> getOptionsForShiftState(bool shift);

    /** Return the cursor-block index (within its lane) or -1 if the cursor is
        over an empty cell. */
    int cursorBlockIndex() const;

    // ── Layout ──
    static constexpr int kLaneLabelWidth = 48;
    static constexpr int kLaneHeightPx   = 32;
    static constexpr int kLaneGapPx      = 4;
    static constexpr int kBarWidthPx     = 24;
    static constexpr int kRulerHeightPx  = 18;

private:
    static constexpr InputManager::BindingId kInvalidBinding = 0;

    void clampCursor();
    void ensureCursorVisible(juce::Rectangle<int> content);
    std::vector<Option> buildOptions(bool shift);
    void restoreRenameStashedWidget();

    // Shared cursor-move primitive used by the stepper handler and the 4D
    // tilt handlers. deltaBar / deltaLane are ±1 nudges; clamps lane to the
    // lane range and bar to ≥ 0.
    void moveCursor(int deltaBar, int deltaLane);

    int cursorLane_ { 0 };
    int cursorBar_  { 0 };
    int viewStartBar_ { 0 };
    int playheadBar_ { -1 };

    // Per-knob fractional accumulator. sensitivity=0.02 means each encoder
    // tick contributes 0.02 to the pending delta; until |accumulator| crosses
    // 1.0 no integer step fires. Without this, small scale * delta * step
    // round-trips to zero and the knob feels dead, then jumps when round
    // trips an int boundary by luck.
    std::array<double, 4> knobAccumulator_ {};

    InputManager::BindingId stepperBinding_ { kInvalidBinding };
    InputManager::BindingId navUpBinding_    { kInvalidBinding };
    InputManager::BindingId navDownBinding_  { kInvalidBinding };
    InputManager::BindingId navLeftBinding_  { kInvalidBinding };
    InputManager::BindingId navRightBinding_ { kInvalidBinding };
    InputManager::BindingId duplicateBinding_ { kInvalidBinding };

    AudioEngine::TransportSnapshot transportSnapshot_;

    // T9 rename of the pattern referenced by the block under the cursor.
    // Identical primitive to PatternWidget::beginPatternRename.
    TextInputComponent renameField_;
    std::unique_ptr<Widget> renameStashedOpposite_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArrangerWidget)
};
