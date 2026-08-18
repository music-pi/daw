#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"

#include "../src/ui/widget/SettingsDialog.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/AudioEngine.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

class SettingsDialogTests : public ::testing::Test
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

TEST_F(SettingsDialogTests, DescriptorIsCorrect)
{
    SettingsDialog dialog;
    auto desc = dialog.describe();

    EXPECT_EQ(desc.id, "settings");
    EXPECT_EQ(desc.pageCount, 1);
    EXPECT_TRUE(desc.forceOnTop);
}

TEST_F(SettingsDialogTests, DefaultBlockSizeIndex)
{
    auto dialog = std::make_unique<SettingsDialog>();
    wm.showDialog(std::move(dialog));

    auto* raw = dynamic_cast<SettingsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->getBlockSizeIndex(), 0); // 128
}

TEST_F(SettingsDialogTests, BlockSizeChangedViaKnob)
{
    auto dialog = std::make_unique<SettingsDialog>();
    wm.showDialog(std::move(dialog));

    auto* raw = dynamic_cast<SettingsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    // k1 (rawIndex 0) = buffer size. Threshold is 12 delta per step.
    raw->handleKnob(0, 12, 0, false);

    // Persist by simulating knob release
    ControllerHost::ButtonEvent release { "knobTouch1", false, false };
    raw->handleButton(release);

    auto settings = harness.audio().getSettingsState();
    EXPECT_EQ(static_cast<int>(settings.getProperty("audioBlockSize")), 256);
}

TEST_F(SettingsDialogTests, BlockSizeRestoredFromState)
{
    auto settings = harness.audio().getSettingsState();
    settings.setProperty("audioBlockSize", 512, nullptr);

    auto dialog = std::make_unique<SettingsDialog>();
    wm.showDialog(std::move(dialog));

    auto* raw = dynamic_cast<SettingsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->getBlockSizeIndex(), 2); // 512
}

TEST_F(SettingsDialogTests, UnsafeLegacyBlockSizeFallsBackTo128)
{
    auto settings = harness.audio().getSettingsState();
    settings.setProperty("audioBlockSize", 64, nullptr);

    auto dialog = std::make_unique<SettingsDialog>();
    wm.showDialog(std::move(dialog));

    auto* raw = dynamic_cast<SettingsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->getBlockSizeIndex(), 0); // 128
}

TEST_F(SettingsDialogTests, RequiredResourcesClaimsKnobs)
{
    SettingsDialog dialog;
    auto resources = dialog.requiredResources(0);

    EXPECT_NE(std::find(resources.begin(), resources.end(), "k1"), resources.end());
    EXPECT_NE(std::find(resources.begin(), resources.end(), "k5"), resources.end());
}

TEST_F(SettingsDialogTests, NoOptionBar)
{
    SettingsDialog dialog;
    EXPECT_TRUE(dialog.getOptions(0).empty());
    EXPECT_TRUE(dialog.getKnobs(0).empty());
}
