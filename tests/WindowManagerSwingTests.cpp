#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"

#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/ui/widget/PatternWidget.h"
#include "../src/engine/AudioEngine.h"

namespace
{

ControllerHost::ButtonEvent makeButton(const std::string& name, bool pressed)
{
    ControllerHost::ButtonEvent e{};
    e.name = name;
    e.pressed = pressed;
    e.shift = false;
    return e;
}

class WindowManagerSwingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        wm.setAudioEngine(&harness.audio());
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    WindowManager wm;
};

TEST_F(WindowManagerSwingTest, SwingHoldPlusEncoderAdjustsGlobally)
{
    auto& audio = harness.audio();
    audio.setSwingPercent(50.0);

    wm.handleButtonEvent(makeButton("swing", true));
    wm.handleButtonEvent(makeButton("navDown", true));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 51.0);

    wm.handleButtonEvent(makeButton("navDown", true));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 52.0);

    wm.handleButtonEvent(makeButton("navUp", true));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 51.0);
}

TEST_F(WindowManagerSwingTest, SwingReleaseStopsAdjusting)
{
    auto& audio = harness.audio();
    audio.setSwingPercent(50.0);

    wm.handleButtonEvent(makeButton("swing", true));
    wm.handleButtonEvent(makeButton("navDown", true));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 51.0);

    wm.handleButtonEvent(makeButton("swing", false));
    wm.handleButtonEvent(makeButton("navDown", true));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 51.0);
}

TEST_F(WindowManagerSwingTest, SwingClampsAtUpperBound)
{
    auto& audio = harness.audio();
    audio.setSwingPercent(75.0);

    wm.handleButtonEvent(makeButton("swing", true));
    wm.handleButtonEvent(makeButton("navDown", true));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 75.0);
}

TEST_F(WindowManagerSwingTest, TempoHoldPlusEncoderAdjustsGlobally)
{
    auto& audio = harness.audio();
    const double initial = audio.getTransportSnapshot().tempoBpm;

    wm.handleButtonEvent(makeButton("tempo", true));
    wm.handleButtonEvent(makeButton("navDown", true));
    EXPECT_DOUBLE_EQ(audio.getTransportSnapshot().tempoBpm, initial + 1.0);

    wm.handleButtonEvent(makeButton("navUp", true));
    EXPECT_DOUBLE_EQ(audio.getTransportSnapshot().tempoBpm, initial);
}

TEST_F(WindowManagerSwingTest, SwingAdjustsEvenWithNonPatternWidgetFocused)
{
    // Install any widget that's NOT Pattern on the left slot.
    // Swing should still adjust globally.
    auto& audio = harness.audio();
    audio.setSwingPercent(50.0);

    // Starting state: no widget installed.
    wm.handleButtonEvent(makeButton("swing", true));
    wm.handleButtonEvent(makeButton("navDown", true));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 51.0);
}

TEST_F(WindowManagerSwingTest, SwingAdjustsWithPatternVisible)
{
    auto& audio = harness.audio();
    audio.setSwingPercent(50.0);

    wm.open(std::make_unique<PatternWidget>(), DisplaySide::Left);

    wm.handleButtonEvent(makeButton("swing", true));
    wm.handleButtonEvent(makeButton("navDown", true));
    EXPECT_DOUBLE_EQ(audio.getSwingPercent(), 51.0);
}

}  // namespace
