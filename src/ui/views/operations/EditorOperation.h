#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../shared/ControlTypes.h"
#include "../../../engine/SamplerInstrument.h"

class AudioEngine;

/**
 * Base interface for audio editor operations (plugins).
 *
 * Each operation handles its own:
 * - UI configuration (knobs, options)
 * - State management
 * - Rendering (overlays on waveform)
 * - Execution logic
 *
 * Operations are used by AudioEditorWidget as overlay renderers
 * and knob/option providers.
 */
class EditorOperation
{
public:
    virtual ~EditorOperation() = default;

    //==========================================================================
    // Lifecycle
    //==========================================================================

    /**
     * Called when this operation becomes active.
     * @param engine The audio engine (may be null)
     */
    virtual void activate(AudioEngine* engine)
    {
        audioEngine = engine;
    }

    /**
     * Called when switching away from this operation.
     */
    virtual void deactivate() = 0;

    //==========================================================================
    // UI Configuration
    //==========================================================================

    /**
     * @return Display name for this operation (e.g., "Truncate", "Normalize")
     */
    virtual juce::String getName() const = 0;

    /**
     * Configure knobs for this operation.
     * @param totalSeconds Total length of the current sample in seconds
     * @return Vector of knob configurations
     */
    virtual std::vector<Knob> getKnobs(double totalSeconds) = 0;

    /**
     * Configure options bar for this operation.
     * @return Vector of option configurations
     */
    virtual std::vector<Option> getOptions() = 0;

    //==========================================================================
    // State Management
    //==========================================================================

    /**
     * Update operation state from a new pad snapshot.
     * Called when the selected pad changes or sample is reloaded.
     * @param snapshot The current pad's snapshot data
     */
    virtual void refreshFromSnapshot(const SamplerInstrument::PadSnapshot& snapshot) = 0;

    //==========================================================================
    // Rendering
    //==========================================================================

    /**
     * Paint operation-specific overlay on the waveform area.
     * @param g Graphics context
     * @param waveformArea Bounds of the waveform display area
     * @param totalSeconds Total sample length in seconds
     */
    virtual void paintOverlay(juce::Graphics& g,
                              const juce::Rectangle<int>& waveformArea,
                              double totalSeconds) = 0;

    //==========================================================================
    // Execution
    //==========================================================================

    /**
     * @return true if the operation can be applied in current state
     */
    virtual bool canApply() const = 0;

    /**
     * Apply the operation to the specified pad.
     * Should create appropriate UndoableAction and execute via undoManager.
     * @param engine The audio engine
     * @param padId The pad to apply the operation to
     * @param undoManager The undo manager for the operation
     */
    virtual void apply(AudioEngine& engine, int padId, juce::UndoManager& undoManager) = 0;

    //==========================================================================
    // Input Handling (optional overrides)
    //==========================================================================

    /**
     * Handle knob delta changes.
     * @param knobIndex Which knob changed
     * @param delta The change amount
     * @param shift Whether shift/fine mode is active
     * @return true if handled, false to let the host handle it
     */
    virtual bool handleKnobDelta(size_t knobIndex, int delta, bool shift) {
        juce::ignoreUnused(knobIndex, delta, shift);
        return false;
    }

    /**
     * Called when a knob's touch state changes.
     * @param knobIndex Which knob
     * @param touched true if now being touched, false if released
     */
    virtual void onKnobTouchStateChanged(size_t knobIndex, bool touched) {
        juce::ignoreUnused(knobIndex, touched);
    }

    /** Set a callback invoked when the operation needs a visual refresh. */
    void setRepaintCallback(std::function<void()> cb) { repaintCallback_ = std::move(cb); }

protected:
    /** Request a visual refresh from the host widget/view. */
    void requestRepaint()
    {
        if (repaintCallback_)
            repaintCallback_();
    }

    // Common state accessible to derived operations
    AudioEngine* audioEngine { nullptr };
    int currentPadId { -1 };
    bool hasSample { false };

private:
    std::function<void()> repaintCallback_;
};
