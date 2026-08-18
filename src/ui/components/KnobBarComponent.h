#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

// Lightweight display bar for up to four controller knobs.
class KnobBarComponent : public juce::Component
{
public:
    struct Slot
    {
        juce::String label;
        juce::String value;
        bool isEnabled { false };
        bool isActive { false }; // True when knob is being touched (knobTouchX pressed)
        bool isInputActive { false }; // True when input is actively being tracked
        bool continuousMode { false }; // If true, callback on every update; if false, only on input resolution
    };

    // Callback for knob drag/adjustment during input (delta-based)
    // Called continuously if continuousMode is true, or only when input becomes inactive if false
    using KnobDragCallback = std::function<void(size_t index, int delta, bool shift)>;
    
    // Callback for knob absolute value updates
    // Called when knobTouch is active for smooth, accurate updates
    using KnobAbsoluteCallback = std::function<void(size_t index, uint16_t absoluteValue, uint16_t maxAbsolute)>;
    
    // Callback when input becomes inactive (resolves the final value)
    // Only called when continuousMode is false
    using KnobInputResolvedCallback = std::function<void(size_t index, double finalValue)>;

    KnobBarComponent();

    void setSlots(const std::array<Slot, 4>& newSlots);
    void setSlot(size_t index, Slot slot);

    void setTitle(const juce::String& title);
    juce::String getTitle() const { return titleText; }

    void setKnobDragCallback(KnobDragCallback callback) { knobDragCallback = std::move(callback); }
    void setKnobAbsoluteCallback(KnobAbsoluteCallback callback) { knobAbsoluteCallback = std::move(callback); }
    void setKnobInputResolvedCallback(KnobInputResolvedCallback callback) { knobInputResolvedCallback = std::move(callback); }

    // Set input active state for a slot (called when knobTouchX pressed/released)
    void setInputActive(size_t index, bool active);

    // Handle knob delta events from controller (called by WindowManager)
    void handleKnobDelta(size_t index, int delta, bool shift);

    // Handle knob absolute events from controller (called by WindowManager)
    void handleKnobAbsolute(size_t index, uint16_t absoluteValue, uint16_t maxAbsolute = 999);

    void paint(juce::Graphics& g) override;
    int getPreferredHeight() const;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    // Test hook — no-op model refreshes must not invalidate the display.
    int getVisualUpdateCountForTest() const { return visualUpdateCount_; }

private:
    size_t getSlotIndexFromPosition(int x) const;
    
    std::array<Slot, 4> slots;
    KnobDragCallback knobDragCallback;
    KnobAbsoluteCallback knobAbsoluteCallback;
    KnobInputResolvedCallback knobInputResolvedCallback;

    juce::String titleText;
    size_t draggedSlotIndex { 0 };
    bool isDragging { false };
    int lastDragY { 0 };
    bool shiftModifier { false };

    // Previous absolute reading per slot for the absolute->delta fallback
    // path in handleKnobAbsolute. Lived as a static-local previously, which
    // would have shared state across all KnobBarComponent instances.
    std::array<uint16_t, 4> lastAbsoluteValues_ { 0, 0, 0, 0 };
    int visualUpdateCount_ { 0 };
};
