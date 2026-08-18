#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"
#include "../src/ui/widget/T9Widget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/AudioEngine.h"
#include "../src/control/HardwareConstants.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

class T9WidgetTests : public ::testing::Test
{
protected:
    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    WindowManager wm;
};

TEST_F(T9WidgetTests, DescriptorIsCorrect)
{
    T9Widget w;
    auto d = w.describe();
    EXPECT_EQ(d.id, "t9");
    EXPECT_EQ(d.pageCount, 1);
    EXPECT_FALSE(d.forceOnTop);
    EXPECT_EQ(d.display, DisplayConstraint::Any);
}

TEST_F(T9WidgetTests, ResourcesLeftPanelIncludePadsAndK1D1)
{
    wm.setAudioEngine(&harness.audio());
    auto widget = std::make_unique<T9Widget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto* raw = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto res = raw->requiredResources(0);
    // All 16 pads
    for (int i = 1; i <= 16; ++i)
        EXPECT_NE(std::find(res.begin(), res.end(), "p" + std::to_string(i)), res.end());
    // Left panel option d1 + knob k1
    EXPECT_NE(std::find(res.begin(), res.end(), "d1"), res.end());
    EXPECT_NE(std::find(res.begin(), res.end(), "k1"), res.end());
}

TEST_F(T9WidgetTests, ResourcesRightPanelUsesD5K5)
{
    wm.setAudioEngine(&harness.audio());
    wm.open(std::make_unique<T9Widget>(), DisplaySide::Right);
    auto* raw = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(raw, nullptr);

    auto res = raw->requiredResources(0);
    EXPECT_NE(std::find(res.begin(), res.end(), "d5"), res.end());
    EXPECT_NE(std::find(res.begin(), res.end(), "k5"), res.end());
}

TEST_F(T9WidgetTests, PadEventForwardsAsOnPadTap)
{
    T9Widget w;
    int lastPad = -1;
    w.setCallbacks({
        .onPadTap      = [&](int idx) { lastPad = idx; },
        .onKnobTurn    = [](int){},
        .onOptionCancel= [](){},
    });

    ControllerHost::PadEvent e;
    e.pad = 7; e.pressed = true;
    w.handlePad(e);
    EXPECT_EQ(lastPad, 7);

    // Pad release is ignored
    e.pressed = false; e.pad = 3;
    lastPad = -1;
    w.handlePad(e);
    EXPECT_EQ(lastPad, -1);
}

TEST_F(T9WidgetTests, KnobForwardsAsOnKnobTurn)
{
    T9Widget w;
    int lastDelta = 0;
    w.setCallbacks({
        .onPadTap=[](int){}, .onKnobTurn=[&](int d){ lastDelta = d; }, .onOptionCancel=[](){},
    });
    w.handleKnob(0, +1, 0, false);     // local index 0 = k1
    EXPECT_EQ(lastDelta, 1);
    w.handleKnob(0, -3, 0, false);
    EXPECT_EQ(lastDelta, -3);
}

TEST_F(T9WidgetTests, LedsPublishedOnActivation)
{
    wm.setAudioEngine(&harness.audio());
    wm.open(std::make_unique<T9Widget>(), DisplaySide::Left);

    auto& hwState = wm.getHardwareState();
    // Letter pads idle at the dim warm-white tier. Full brightness is reserved
    // for the active cycle pad (pendingPad_) while typing.
    const uint8_t val = hwState.getLed("p2");
    EXPECT_EQ(val, HardwareConstants::kColorWhiteDim);
}

TEST_F(T9WidgetTests, NumPadBrightnessFollowsMode)
{
    wm.setAudioEngine(&harness.audio());
    wm.open(std::make_unique<T9Widget>(), DisplaySide::Left);

    auto* raw = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto& hwState = wm.getHardwareState();
    // Pad 7 is now the num-mode toggle (caps moved to the hardware Shift
    // button). The pad's colour reflects T9StateMachine::Mode, not Caps.
    raw->setMode(T9StateMachine::Mode::Letters);
    EXPECT_EQ(hwState.getLed("p8"), HardwareConstants::kColorGoldDim);

    raw->setMode(T9StateMachine::Mode::Numbers);
    EXPECT_EQ(hwState.getLed("p8"), HardwareConstants::kColorGoldBright);
}

TEST_F(T9WidgetTests, LedsClearedOnDeactivation)
{
    wm.setAudioEngine(&harness.audio());
    wm.open(std::make_unique<T9Widget>(), DisplaySide::Left);
    wm.closeAll();

    auto& hwState = wm.getHardwareState();
    EXPECT_EQ(hwState.getLed("p2"), HardwareConstants::kColorOff);
}
