#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"
#include "../src/control/HardwareConstants.h"

#include "harness/EngineHarness.h"
#include "../src/ui/widget/TransportWidget.h"
#include "../src/ui/hw/HardwareState.h"

namespace
{

class TransportWidgetTest : public ::testing::Test
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
        // Stop the timer immediately to prevent background interference in tests
        widget.onDeactivated();
    }

    testharness::EngineHarness harness;
    HardwareState hw;
    TransportWidget widget;
};

// 1. Resource declaration
TEST_F(TransportWidgetTest, RequiredResourcesReturnsTransportLedIds)
{
    auto resources = widget.requiredResources(0);

    ASSERT_EQ(resources.size(), 4u);
    EXPECT_EQ(resources[0], "play");
    EXPECT_EQ(resources[1], "stop");
    EXPECT_EQ(resources[2], "recCountIn");
    EXPECT_EQ(resources[3], "restartLoop");
}

// 2. Descriptor
TEST_F(TransportWidgetTest, DescriptorHasCorrectIdAndZeroPages)
{
    auto desc = widget.describe();
    EXPECT_EQ(desc.id, juce::String("transport"));
    EXPECT_EQ(desc.pageCount, 0);
    EXPECT_FALSE(desc.forceOnTop);
}

// 3. Play toggle — pressing play starts playback
TEST_F(TransportWidgetTest, PlayButtonStartsPlayback)
{
    EXPECT_FALSE(harness.audio().isPlaying());

    ControllerHost::ButtonEvent playPress;
    playPress.name = "play";
    playPress.pressed = true;
    widget.handleButton(playPress);

    EXPECT_TRUE(harness.audio().isPlaying());
}

// 4. Stop — pressing stop stops playback
TEST_F(TransportWidgetTest, StopButtonStopsPlayback)
{
    harness.audio().play();
    EXPECT_TRUE(harness.audio().isPlaying());

    ControllerHost::ButtonEvent stopPress;
    stopPress.name = "stop";
    stopPress.pressed = true;
    widget.handleButton(stopPress);

    EXPECT_FALSE(harness.audio().isPlaying());
}

// 5. Loop toggle
TEST_F(TransportWidgetTest, LoopButtonTogglesLoop)
{
    bool loopBefore = harness.audio().isLooping();

    ControllerHost::ButtonEvent loopPress;
    loopPress.name = "restartLoop";
    loopPress.pressed = true;
    widget.handleButton(loopPress);

    EXPECT_NE(harness.audio().isLooping(), loopBefore);
}

// 6. LED state — play LED brightness matches transport state
TEST_F(TransportWidgetTest, PlayLedBrightWhenPlaying)
{
    harness.audio().play();

    // Manually trigger LED update (normally done by timer)
    widget.onActivated(0);
    widget.onDeactivated(); // stop timer

    auto playVal = hw.getLed("play");
    EXPECT_EQ(playVal, HardwareConstants::kLedBright);
}

TEST_F(TransportWidgetTest, StopLedBrightWhenStopped)
{
    // Not playing, so stop should be bright
    widget.onActivated(0);
    widget.onDeactivated();

    auto stopVal = hw.getLed("stop");
    EXPECT_EQ(stopVal, HardwareConstants::kLedBright);
}

TEST_F(TransportWidgetTest, RecordLedBrightImmediatelyWhenArmedAndStopped)
{
    ASSERT_FALSE(harness.audio().isPlaying());
    harness.audio().setRecordArmed(true);

    widget.onUiHostTick();

    EXPECT_EQ(hw.getLed("recCountIn"), HardwareConstants::kLedBright);
}

TEST_F(TransportWidgetTest, AudioCaptureSuiteOwnsRecordLedSemantics)
{
    harness.audio().setAudioCaptureState(true, false);
    widget.onUiHostTick();
    EXPECT_EQ(hw.getLed("recCountIn"),
              HardwareConstants::kLedMedium);

    harness.audio().setAudioCaptureState(true, true);
    widget.onUiHostTick();
    EXPECT_EQ(hw.getLed("recCountIn"),
              HardwareConstants::kLedBright);

    harness.audio().setAudioCaptureState(false, false);
}

// 7. Button release is ignored
TEST_F(TransportWidgetTest, ButtonReleaseIsIgnored)
{
    EXPECT_FALSE(harness.audio().isPlaying());

    ControllerHost::ButtonEvent releaseEvent;
    releaseEvent.name = "play";
    releaseEvent.pressed = false;
    widget.handleButton(releaseEvent);

    EXPECT_FALSE(harness.audio().isPlaying());
}

// 8. Unrelated button is ignored
TEST_F(TransportWidgetTest, UnrelatedButtonIsIgnored)
{
    ControllerHost::ButtonEvent unknownBtn;
    unknownBtn.name = "someOtherButton";
    unknownBtn.pressed = true;
    widget.handleButton(unknownBtn);

    // Should not crash or change state
    EXPECT_FALSE(harness.audio().isPlaying());
}

} // namespace
