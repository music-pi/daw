#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include <juce_events/juce_events.h>

#include "Widget.h"
#include "../components/PadGridComponent.h"
#include "../components/T9StateMachine.h"

/**
 * T9Widget — phone-keypad text input widget.
 *
 * The 4×4 pad grid is rendered via PadGridComponent (child component).
 * T9Widget builds a Tile array from kTiles[] + runtime state on every
 * refreshTiles() call and pushes it to the grid. LED publishing remains
 * a direct hw().setLed() path so that onDeactivated can reliably clear
 * all 16 LEDs without routing through the component hierarchy.
 *
 * The candidate strip at the bottom of the panel is still painted
 * directly in paintPage().
 */
class T9Widget : public Widget
{
public:
    struct Callbacks
    {
        std::function<void(int padIndex)> onPadTap {};
        std::function<void(int deltaSteps)> onKnobTurn {};
        std::function<void()> onOptionCancel {};
        std::function<void()> onOptionOk {};
        std::function<void()> onShiftPress {};   // hw Shift press → cycle caps
    };

    T9Widget();
    ~T9Widget() override;

    void setCallbacks(Callbacks cb);
    void setOnDeactivated(std::function<void()> cb);

    // Display state pushed down by TextInputComponent:
    void setPendingCycleHint(int padIndex, juce::juce_wchar previewChar);  // padIndex=-1 clears
    void setCandidates(std::vector<juce::String> items, int selectedIndex);
    void setCapsMode(T9StateMachine::Caps caps);
    void setMode(T9StateMachine::Mode mode);

    // Widget API:
    WidgetDescriptor describe() const override;
    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    std::vector<std::string> requiredResources(int page) override;
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void onUiHostTick() override;
    void handlePad(const controller_events::PadEvent& e) override;
    void handleKnob(int localIndex, int16_t delta, uint16_t absolute, bool shift) override;

private:
    Callbacks callbacks_;
    std::function<void()> onDeactivated_;
    int pendingPad_ = -1;
    juce::juce_wchar pendingChar_ = 0;
    std::vector<juce::String> candidates_;
    int selectedCandidate_ = 0;
    T9StateMachine::Caps caps_ = T9StateMachine::Caps::Lower;
    T9StateMachine::Mode mode_ = T9StateMachine::Mode::Letters;

    /** Build tile array from kTiles + current state, push to grid_. */
    void refreshTiles();

    /** Publish LED colors for all 16 pads directly via hw().setLed(). */
    void publishLeds();

    /** Atomic refresh of both tile visuals and LED state. Call this instead
        of publishLeds() + refreshTiles() separately to ensure they can't
        drift when a new state variable is added. */
    void refreshGridState();

    // Flash animation driven off Widget::onUiHostTick() — ticked at 60Hz
    // by UiHost, so we don't need to own a private juce::Timer here.
    void startFlashTimer();
    void stopFlashTimer();

    juce::int64 flashStartedMs_ = 0;
    bool flashActive_ { false };

    // ── Child components ─────────────────────────────────────────────────
    PadGridComponent grid_;

    // ── Constants ────────────────────────────────────────────────────────
    static constexpr int kCandidateStripPx = 32;

    // Priority pad-handler binding registered on activation to intercept pad
    // events before the global sampler-trigger handler. Holds kInvalidBinding
    // when no handler is registered.
    static constexpr std::uint32_t kInvalidBinding = 0;
    std::uint32_t padBinding_ = kInvalidBinding;

    // Shift-button handler binding — cycles caps on press while T9 is active.
    std::uint32_t shiftBinding_ = kInvalidBinding;
    std::function<void()> onShiftPress_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T9Widget)
};
