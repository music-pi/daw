#include <gtest/gtest.h>

#include "../src/control/HardwareConstants.h"
#include "../src/engine/MidiConstants.h"
#include "../src/ui/widget/KeyboardWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

class KeyboardWidgetTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        windowManager.setAudioEngine(&engine.audio());
        windowManager.open(std::make_unique<KeyboardWidget>(), DisplaySide::Right);
        widget = dynamic_cast<KeyboardWidget*>(
            windowManager.getWidget(DisplaySide::Right));
        ASSERT_NE(widget, nullptr);
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness engine;
    WindowManager windowManager;
    KeyboardWidget* widget { nullptr };
};

TEST_F(KeyboardWidgetTests, HeldPadBrightnessTracksEmittedVelocity)
{
    auto& hardware = windowManager.getHardwareState();

    widget->setPadFlashed(0, true, 1);
    EXPECT_EQ(hardware.getLed("p1"), HardwareConstants::kColorGreenLowest);

    widget->setPadFlashed(0, true, 32);
    EXPECT_EQ(hardware.getLed("p1"),
              static_cast<uint8_t>(HardwareConstants::kColorGreenLowest + 1));

    widget->setPadFlashed(0, true, midi::kVelocityMax);
    EXPECT_EQ(hardware.getLed("p1"), HardwareConstants::kColorGreenHighest);
}

TEST_F(KeyboardWidgetTests, PadReleaseRestoresKeyboardLayoutColour)
{
    auto& hardware = windowManager.getHardwareState();

    widget->setPadFlashed(0, true, midi::kVelocityMax);
    ASSERT_EQ(hardware.getLed("p1"), HardwareConstants::kColorGreenHighest);

    widget->setPadFlashed(0, false, 0);
    EXPECT_EQ(hardware.getLed("p1"), HardwareConstants::kColorWhite);
}

TEST_F(KeyboardWidgetTests, SixteenVelocityModeKeepsChromaticPadLayout)
{
    widget->setVelocityMode(false, true);
    auto& hardware = windowManager.getHardwareState();

    EXPECT_EQ(hardware.getLed("p1"), HardwareConstants::kColorWhite);
    EXPECT_EQ(hardware.getLed("p2"), HardwareConstants::kColorOff);
}

TEST(KeyboardVelocityTests, PressureVelocityQuantizesToSixteenLevels)
{
    EXPECT_EQ(midi::quantizeToSixteenVelocityLevels(1), 8);
    EXPECT_EQ(midi::quantizeToSixteenVelocityLevels(8), 8);
    EXPECT_EQ(midi::quantizeToSixteenVelocityLevels(9), 16);
    EXPECT_EQ(midi::quantizeToSixteenVelocityLevels(100), 104);
    EXPECT_EQ(midi::quantizeToSixteenVelocityLevels(127), 127);
}
