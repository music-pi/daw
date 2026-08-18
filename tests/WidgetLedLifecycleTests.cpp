#include <gtest/gtest.h>

#include "../src/ui/widget/WindowManager.h"
#include "../src/ui/widget/PadOverviewWidget.h"
#include "../src/ui/widget/PatternWidget.h"
#include "../src/control/HardwareConstants.h"
#include "../src/engine/AudioEngine.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

class WidgetLedLifecycleTest : public ::testing::Test
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

// Opening PadOverviewWidget claims pad LEDs and sets values
TEST_F(WidgetLedLifecycleTest, OpenPadOverviewClaimsPadLeds)
{
    wm.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Left);

    auto& hw = wm.getHardwareState();

    // Pad LEDs should be claimed by pad_overview
    for (int i = 1; i <= 16; ++i)
    {
        auto rid = "p" + std::to_string(i);
        EXPECT_TRUE(hw.isClaimed(rid)) << rid << " should be claimed";
        EXPECT_EQ(hw.getOwner(rid), "pad_overview");
    }
}

// Opening PatternWidget claims pad LEDs, step button, and screen button
TEST_F(WidgetLedLifecycleTest, OpenPatternClaimsPadLedsAndButtons)
{
    wm.open(std::make_unique<PatternWidget>(), DisplaySide::Left);

    auto& hw = wm.getHardwareState();

    for (int i = 1; i <= 16; ++i)
    {
        auto rid = "p" + std::to_string(i);
        EXPECT_TRUE(hw.isClaimed(rid)) << rid << " should be claimed";
        EXPECT_EQ(hw.getOwner(rid), "pattern");
    }

    EXPECT_TRUE(hw.isClaimed("step"));
    EXPECT_EQ(hw.getOwner("step"), "pattern");

    EXPECT_TRUE(hw.isClaimed("pattern"));
    EXPECT_EQ(hw.getLed("pattern"), HardwareConstants::kLedBright);
}

// Switching from PadOverview to Pattern transfers pad LED ownership
TEST_F(WidgetLedLifecycleTest, SwitchWidgetTransfersPadLeds)
{
    wm.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Left);

    auto& hw = wm.getHardwareState();
    EXPECT_EQ(hw.getOwner("p1"), "pad_overview");

    // Open PatternWidget on same side — should take over pad LEDs
    wm.open(std::make_unique<PatternWidget>(), DisplaySide::Left);

    EXPECT_EQ(hw.getOwner("p1"), "pattern");
    EXPECT_FALSE(hw.isClaimed("d1") && hw.getOwner("d1") == "pad_overview")
        << "Old widget resources should be released";
}

TEST_F(WidgetLedLifecycleTest, ReclaimResourcesReturnsSharedPadLedsToFocusedMode)
{
    wm.open(std::make_unique<PatternWidget>(), DisplaySide::Left);
    wm.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Right);

    auto& hw = wm.getHardwareState();
    EXPECT_EQ(hw.getOwner("p1"), "pad_overview");

    wm.reclaimResources(DisplaySide::Left);

    EXPECT_EQ(hw.getOwner("p1"), "pattern");
    EXPECT_EQ(hw.getOwner("p16"), "pattern");
}

// Closing widget releases resources and resets LED values
TEST_F(WidgetLedLifecycleTest, CloseWidgetReleasesAndResetsLeds)
{
    wm.open(std::make_unique<PatternWidget>(), DisplaySide::Left);

    auto& hw = wm.getHardwareState();
    EXPECT_TRUE(hw.isClaimed("pattern"));
    EXPECT_EQ(hw.getLed("pattern"), HardwareConstants::kLedBright);

    wm.close("pattern");

    EXPECT_FALSE(hw.isClaimed("pattern"));
    EXPECT_EQ(hw.getLed("pattern"), 0);

    for (int i = 1; i <= 16; ++i)
    {
        auto rid = "p" + std::to_string(i);
        EXPECT_FALSE(hw.isClaimed(rid)) << rid << " should be released";
        EXPECT_EQ(hw.getLed(rid), 0) << rid << " LED should be off";
    }
}

// Flush sends dirty values and clears dirty flags
TEST_F(WidgetLedLifecycleTest, FlushSendsPatternScreenButtonLed)
{
    // Use a mock flush target to verify output
    class RecordingTarget : public HardwareState::FlushTarget
    {
    public:
        void setMonoLed(const std::string& name, uint8_t brightness) override
        {
            monoLeds[name] = brightness;
        }
        void setIndexedLed(const std::string& name, uint8_t colorIndex) override
        {
            indexedLeds[name] = colorIndex;
        }
        void setButtonBrightness(const std::string& name, uint8_t brightness) override
        {
            buttonLeds[name] = brightness;
        }
        std::unordered_map<std::string, uint8_t> monoLeds;
        std::unordered_map<std::string, uint8_t> indexedLeds;
        std::unordered_map<std::string, uint8_t> buttonLeds;
    };

    wm.open(std::make_unique<PatternWidget>(), DisplaySide::Left);

    auto& hw = wm.getHardwareState();
    RecordingTarget target;
    hw.flush(target);

    // Screen button should have been flushed as button LED
    EXPECT_EQ(target.buttonLeds["pattern"], HardwareConstants::kLedBright);

    // Step button should have been flushed as button LED
    auto stepIt = target.buttonLeds.find("step");
    EXPECT_NE(stepIt, target.buttonLeds.end());
}

// System widgets (transport, groups) claim their resources on init
TEST_F(WidgetLedLifecycleTest, SystemWidgetsClaimResources)
{
    wm.initSystemWidgets();

    auto& hw = wm.getHardwareState();

    // Transport LEDs claimed
    EXPECT_TRUE(hw.isClaimed("play"));
    EXPECT_TRUE(hw.isClaimed("stop"));
    EXPECT_EQ(hw.getOwner("play"), "transport");

    // Group LEDs claimed
    EXPECT_TRUE(hw.isClaimed("g1"));
    EXPECT_TRUE(hw.isClaimed("g8"));
    EXPECT_EQ(hw.getOwner("g1"), "groups");
}
