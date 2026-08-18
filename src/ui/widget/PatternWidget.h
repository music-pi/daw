#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widget.h"
#include "../components/TextInputComponent.h"
#include "../../engine/AudioEngine.h"

/**
 * PatternWidget -- step sequencer grid with channel selection,
 * step toggling (undoable via ToggleSequencerStepCommand), tempo
 * display, and playhead animation.
 *
 * Uses the Widget base class; no consumer interfaces.
 */
class PatternWidget : public Widget
{
public:
    PatternWidget();
    ~PatternWidget() override;

    // -- Widget identity --
    WidgetDescriptor describe() const override;

    // -- Lifecycle --
    void onActivated(int panelOffset) override;
    void onDeactivated() override;

    // -- Resources --
    std::vector<std::string> requiredResources(int page) override;

    // -- Options & Knobs --
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    // -- Rendering --
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    // -- Input --
    void handlePad(const controller_events::PadEvent& e) override;
    void handleButton(const controller_events::ButtonEvent& e) override;
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;

    // -- Shift overlay --
    void refreshPadLeds() override { refreshPadLedState(); }

    // -- Accessors --
    int getSelectedChannelPadId() const { return selectedChannelPadId_; }
    int getSelectedPatternIndex() const { return selectedPatternIndex_; }
    bool isStepModeActive() const { return stepModeActive_; }
    bool isReplaceMode() const { return replaceMode_; }

    /** Channel filter: Pads shows pads with a sample + no instrument
        (drum kit rows), Instruments shows pads carrying an ExternalPlugin
        instrument (tonal rows). UiHost flips the mode when entering /
        exiting keyboard mode so each workflow sees its own row set. */
    enum class ChannelMode { Pads, Instruments };
    void setChannelMode(ChannelMode mode);
    ChannelMode getChannelMode() const { return channelMode_; }

    /** Select a specific pad as the active channel. Used by UiHost to keep
        PatternWidget's selection in sync with the keyboard bank's active
        pad. */
    void selectChannel(int padId);

    /** Toggle step mode on/off. When on, pads map to steps. */
    void toggleStepMode();

    /** Navigate channel selection up (direction < 0) or down (direction > 0). */
    void navigateChannel(int direction);

    /** Cycle selectedStep_ through the currently active steps of the selected channel. */
    void cycleSelectedStep(int direction);

    /** Switch to the previous pattern (no-op if at pattern 0). */
    void selectPrevPattern();

    /** Switch to the next pattern, creating it on demand if it doesn't exist
        yet. New patterns inherit the current bars/stepsPerBar. */
    void selectNextPattern();

    /** Opens the note editor for the selected instrument channel. UiHost owns
        the panel stash/restore flow, so it supplies the callback. */
    void setOnPianoRollRequested(std::function<void()> callback)
    {
        onPianoRollRequested_ = std::move(callback);
    }

    /** Called from the UiHost tick (60 Hz) to refresh transport-driven state. */
    void onUiHostTick() override;

private:
    struct StepCell
    {
        int padId { -1 };
        int patternIndex { -1 };
        int stepIndex { -1 };
        juce::Rectangle<float> bounds;
        bool lit { false };
    };

    void autoSelectFirstChannel();
    void refreshPadLedState();
    void updateStepButtonLed();
    int getStepCount() const;
    double getPlayheadStep() const;
    void toggleStepAtPad(int padIndex);
    void recordStepAtPlayhead(int padIndex);
    void eraseSelectedChannel();
    /** Stash the right-panel widget, seed renameField_ with the current
        pattern's name, and open the T9 flow. Restored on commit / cancel. */
    void beginPatternRename();
    void restoreRenameStashedWidget();

    AudioEngine::TransportSnapshot transportSnapshot_;
    std::vector<StepCell> stepCells_;

    int selectedChannelPadId_ { -1 };
    int selectedPatternIndex_ { -1 };
    int selectedStep_ { -1 };           // focused step for velocity editing (-1 = none)
    bool stepModeActive_ { false };
    int lastCursorStep_ { -1 };
    int currentPatternNumber_ { 0 };  // 0-based, display as 1-based
    int barsPerPattern_ { 1 };
    int stepsPerBar_ { 16 };

    // Signature of the last frame we painted; used by onUiHostTick to skip
    // no-op repaints when nothing visible would change. Recomputed cheaply
    // from transport + selection + pattern-shape fields every tick.
    std::size_t lastRepaintSignature_ { 0 };
    std::size_t computeRepaintSignature() const;
    std::array<int, 4> knobAccumulator_ {};
    bool velocityKnobTransactionOpen_ { false };
    bool selectHeld_ { false };
    bool replaceMode_ { false };
    bool clickWasEnabled_ { false };  // restore click state after recording
    ChannelMode channelMode_ { ChannelMode::Pads };
    std::function<void()> onPianoRollRequested_;

    juce::Rectangle<int> contentArea_;

    // T9 driver for the rename flow. The component never renders itself
    // (addChildComponent, not addAndMakeVisible) — it just owns the state
    // machine and spawns a T9Widget on the opposite panel while editing.
    TextInputComponent renameField_;
    // Right-panel widget stashed while the T9 is up, popped back when the
    // rename commits or cancels. Mirrors UiHost's showFileDialog stash.
    std::unique_ptr<Widget> renameStashedRight_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternWidget)
};
