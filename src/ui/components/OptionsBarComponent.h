#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../shared/ControlTypes.h"

// Visualises up to four option slots matching the Maschine MK3 button row.
class OptionsBarComponent : public juce::Component
{
public:
    struct Option
    {
        juce::String label;
        OptionState state { OptionState::Enabled };
        int span { 1 };
        bool isOperationNavigator { false }; // If true, left/right parts are separately clickable
    };

    OptionsBarComponent();
    ~OptionsBarComponent() override = default;

    void setOptions(const std::vector<Option>& options);
    void paint(juce::Graphics& g) override;

    // Render a slot as Active while the hardware button is physically held down.
    // Pass -1 to clear the pressed state.
    void setPressedSlot(int slotIndex);

    // Get the option index for a given slot position (0-3)
    // Returns -1 if slot is empty or invalid
    int getOptionIndexForSlot(size_t slotIndex) const;

    std::function<void(size_t index)> onOptionTriggered;
    std::function<void(size_t index, bool isLeftPart)> onOperationNavigatorTriggered; // For operation navigator: index and which part was clicked

    // Test hook — count of times setOptions actually ran through the layout
    // path (no-op calls against identical data do not increment this).
    int getRebuildCountForTest() const { return rebuildCount_; }

private:
    struct Slot
    {
        Option option;
        bool hasOption { false };
        bool isStart { false };
        int span { 1 };
        int optionIndex { -1 };
        int startSlot { -1 };
    };

    static constexpr int kMaxSlots = 4;
    std::array<Slot, kMaxSlots> slots{};
    int pressedSlot_ = -1;
    void mouseUp(const juce::MouseEvent& event) override;

    // Lightweight mirror of the last options seen, minus any callbacks or
    // non-comparable fields. Used by setOptions() to short-circuit when the
    // visible state is identical — avoids repaint + resize churn when
    // setOptions is called from per-tick refresh paths.
    struct OptionSnapshot
    {
        juce::String label;
        OptionState state { OptionState::Enabled };
        int span { 1 };
        bool isOperationNavigator { false };
    };
    std::vector<OptionSnapshot> previousOptions_;
    int rebuildCount_ { 0 };
};
