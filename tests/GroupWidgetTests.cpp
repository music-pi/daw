#include <gtest/gtest.h>

#include "harness/EngineHarness.h"
#include "../src/control/ControllerHost.h"
#include "../src/ui/widget/GroupWidget.h"
#include "../src/ui/hw/HardwareState.h"
#include "../src/engine/GroupManager.h"
#include "../src/ui/theme/UiTheme.h"

namespace
{

class GroupWidgetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        widget.setHardware(&hw);
        widget.setAudioEngine(&harness.audio());

        // Claim resources as WindowManager would
        auto resources = widget.requiredResources(0);
        auto id = widget.describe().id.toStdString();
        for (const auto& rid : resources)
            hw.claim(rid, id);

        widget.onActivated(0);
    }

    testharness::EngineHarness harness;
    HardwareState hw;
    GroupWidget widget;
};

// 1. Resource declaration — returns g1-g8
TEST_F(GroupWidgetTest, RequiredResourcesReturnsGroupLedIds)
{
    auto resources = widget.requiredResources(0);

    ASSERT_EQ(resources.size(), 8u);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(resources[static_cast<size_t>(i)], "g" + std::to_string(i + 1));
}

// 2. Descriptor
TEST_F(GroupWidgetTest, DescriptorHasCorrectIdAndZeroPages)
{
    auto desc = widget.describe();
    EXPECT_EQ(desc.id, juce::String("groups"));
    EXPECT_EQ(desc.pageCount, 0);
    EXPECT_FALSE(desc.forceOnTop);
}

// 3. Group recall — pressing g1 when group has data recalls it
TEST_F(GroupWidgetTest, GroupRecallOnPress)
{
    auto& gm = harness.audio().getGroupManager();

    // Create and save group 0 so it has data
    gm.createGroup(0);
    gm.saveCurrentState(0);

    // Press g1 to recall
    ControllerHost::ButtonEvent g1Press;
    g1Press.name = "g1";
    g1Press.pressed = true;
    widget.handleButton(g1Press);

    EXPECT_TRUE(gm.isGroupActive(0));
}

// 4. LED colors — active group is brightest variant
TEST_F(GroupWidgetTest, ActiveGroupLedIsBrightest)
{
    auto& gm = harness.audio().getGroupManager();
    gm.createGroup(0);
    gm.saveCurrentState(0);
    gm.recallGroup(0);

    widget.updateLeds();

    uint8_t color = gm.getGroupColor(0);
    uint8_t expected = UiTheme::brightestHueVariant(color);
    EXPECT_EQ(hw.getLed("g1"), expected);
}

// 5. LED colors — group with data but not active is dimmest
TEST_F(GroupWidgetTest, DataGroupLedIsDimmest)
{
    auto& gm = harness.audio().getGroupManager();

    // Create and save two groups: g1 and g2
    gm.createGroup(0);
    gm.saveCurrentState(0);
    gm.createGroup(1);
    gm.saveCurrentState(1);

    // Recall g2 so g1 is inactive but has data
    gm.recallGroup(1);

    widget.updateLeds();

    uint8_t color0 = gm.getGroupColor(0);
    uint8_t expected0 = UiTheme::dimmestHueVariant(color0);
    EXPECT_EQ(hw.getLed("g1"), expected0);
}

// 6. LED colors — empty group (no data, not active) is off (0)
TEST_F(GroupWidgetTest, EmptyGroupLedIsOff)
{
    widget.updateLeds();

    // GroupManager constructor makes group 0 active by default,
    // so check a group that is neither active nor has data.
    // Groups 1-7 have no data and are not active.
    auto& gm = harness.audio().getGroupManager();
    for (int i = 1; i < 8; ++i)
    {
        if (!gm.isGroupActive(i) && !gm.hasGroupData(i))
        {
            EXPECT_EQ(hw.getLed("g" + std::to_string(i + 1)), 0u)
                << "Group " << i << " should have LED off";
        }
    }
}

// 7. Button release is ignored
TEST_F(GroupWidgetTest, ButtonReleaseIsIgnored)
{
    auto& gm = harness.audio().getGroupManager();
    int activeBeforeRelease = gm.getActiveGroupIndex();

    ControllerHost::ButtonEvent releaseEvent;
    releaseEvent.name = "g2";
    releaseEvent.pressed = false;
    widget.handleButton(releaseEvent);

    // Should not crash, active group unchanged
    EXPECT_EQ(gm.getActiveGroupIndex(), activeBeforeRelease);
}

// 8. Unrelated button is ignored
TEST_F(GroupWidgetTest, UnrelatedButtonIsIgnored)
{
    ControllerHost::ButtonEvent unknownBtn;
    unknownBtn.name = "play";
    unknownBtn.pressed = true;
    widget.handleButton(unknownBtn);

    // Should not crash
}

} // namespace
