#include <gtest/gtest.h>

#include "../src/ui/components/KnobBarComponent.h"

namespace
{
KnobBarComponent::Slot makeSlot(const juce::String& label,
                                const juce::String& value)
{
    KnobBarComponent::Slot slot;
    slot.label = label;
    slot.value = value;
    slot.isEnabled = true;
    slot.continuousMode = true;
    return slot;
}
} // namespace

TEST(KnobBarComponentTests, IdenticalSlotsDoNotInvalidateVisuals)
{
    KnobBarComponent bar;
    std::array<KnobBarComponent::Slot, 4> slots{};
    slots[0] = makeSlot("Volume", "0.0 dB");

    bar.setSlots(slots);
    const int afterFirstUpdate = bar.getVisualUpdateCountForTest();
    bar.setSlots(slots);

    EXPECT_EQ(bar.getVisualUpdateCountForTest(), afterFirstUpdate);
}

TEST(KnobBarComponentTests, VisibleSlotChangeInvalidatesVisuals)
{
    KnobBarComponent bar;
    std::array<KnobBarComponent::Slot, 4> slots{};
    slots[0] = makeSlot("Volume", "0.0 dB");
    bar.setSlots(slots);
    const int beforeChange = bar.getVisualUpdateCountForTest();

    slots[0].value = "-3.0 dB";
    bar.setSlots(slots);

    EXPECT_EQ(bar.getVisualUpdateCountForTest(), beforeChange + 1);
}

TEST(KnobBarComponentTests, IdenticalSingleSlotDoesNotInvalidateVisuals)
{
    KnobBarComponent bar;
    auto slot = makeSlot("Pan", "Centre");
    bar.setSlot(1, slot);
    const int afterFirstUpdate = bar.getVisualUpdateCountForTest();

    bar.setSlot(1, slot);

    EXPECT_EQ(bar.getVisualUpdateCountForTest(), afterFirstUpdate);
}
