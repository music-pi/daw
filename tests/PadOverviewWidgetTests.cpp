#include <gtest/gtest.h>

#include "../src/ui/widget/PadOverviewWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/control/HardwareConstants.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

class PadOverviewWidgetTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost mockController;
    WindowManager wm;
};

// 1. Descriptor
TEST_F(PadOverviewWidgetTests, DescriptorIsCorrect)
{
    PadOverviewWidget widget;
    auto desc = widget.describe();

    EXPECT_EQ(desc.id, "pad_overview");
    EXPECT_EQ(desc.pageCount, 1);
    EXPECT_FALSE(desc.forceOnTop);
    EXPECT_EQ(desc.display, DisplayConstraint::Any);
}

// 2. Resource declaration includes p1-p16 and option/knob IDs
TEST_F(PadOverviewWidgetTests, ResourceDeclarationLeftPanel)
{
    wm.setAudioEngine(&harness.audio());
    auto widget = std::make_unique<PadOverviewWidget>();
    auto* raw = widget.get();

    // Before activation, panelOffset defaults to 0 (left)
    auto resources = raw->requiredResources(0);

    // p1-p16
    for (int i = 1; i <= 16; ++i)
        EXPECT_NE(std::find(resources.begin(), resources.end(), "p" + std::to_string(i)), resources.end())
            << "Missing p" << i;

    // Left panel: d1-d4, k1-k4
    for (int i = 1; i <= 4; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Missing k" << i;
    }

    // Should NOT have right panel resources
    for (int i = 5; i <= 8; ++i)
    {
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Should not have d" << i;
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Should not have k" << i;
    }
}

TEST_F(PadOverviewWidgetTests, ResourceDeclarationRightPanel)
{
    wm.setAudioEngine(&harness.audio());

    // Open on Right side to get panelOffset=1
    auto widget = std::make_unique<PadOverviewWidget>();
    wm.open(std::move(widget), DisplaySide::Right);

    auto* raw = static_cast<PadOverviewWidget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(raw, nullptr);

    // After activation with panelOffset=1, requiredResources should return right-side resources
    auto resources = raw->requiredResources(0);

    // p1-p16 should still be present
    for (int i = 1; i <= 16; ++i)
        EXPECT_NE(std::find(resources.begin(), resources.end(), "p" + std::to_string(i)), resources.end());

    // Right panel: d5-d8, k5-k8
    for (int i = 5; i <= 8; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Missing k" << i;
    }
}

// 3. Activation lifecycle
TEST_F(PadOverviewWidgetTests, ActivationClaimsResourcesDeactivationReleases)
{
    wm.setAudioEngine(&harness.audio());

    auto widget = std::make_unique<PadOverviewWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto& hw = wm.getHardwareState();

    // After opening, resources should be claimed
    EXPECT_TRUE(hw.isClaimed("p1"));
    EXPECT_TRUE(hw.isClaimed("p16"));
    EXPECT_TRUE(hw.isClaimed("d1"));
    EXPECT_TRUE(hw.isClaimed("k1"));
    EXPECT_EQ(hw.getOwner("p1"), "pad_overview");

    // Close the widget
    wm.close("pad_overview");

    // Resources should be released
    EXPECT_FALSE(hw.isClaimed("p1"));
    EXPECT_FALSE(hw.isClaimed("p16"));
    EXPECT_FALSE(hw.isClaimed("d1"));
}

// 4. Modifier LED state
TEST_F(PadOverviewWidgetTests, ActivationPublishesModifierLedState)
{
    wm.setAudioEngine(&harness.audio());
    wm.setControllerHost(&mockController);

    auto widget = std::make_unique<PadOverviewWidget>();
    auto* raw = widget.get();
    wm.open(std::move(widget), DisplaySide::Left);

    const auto& hw = wm.getHardwareState();
    EXPECT_EQ(hw.getLed("solo"), HardwareConstants::kLedDim);
    EXPECT_EQ(hw.getLed("muteChoke"), HardwareConstants::kLedDim);

    raw->handleButton({ "solo", true, false });
    EXPECT_EQ(hw.getLed("solo"), HardwareConstants::kLedBright);
    EXPECT_EQ(hw.getLed("muteChoke"), HardwareConstants::kLedDim);
}

// 5. Pad selection — simulate pad press with select=true
TEST_F(PadOverviewWidgetTests, PadSelectionWhenSelectHeld)
{
    wm.setAudioEngine(&harness.audio());
    wm.setControllerHost(&mockController);

    auto sample = harness.createTemporarySampleFile("sel_test", 44100);
    harness.pads().loadSample(0, sample);

    auto widget = std::make_unique<PadOverviewWidget>();
    wm.open(std::move(widget), DisplaySide::Left);
    wm.setFocus(DisplaySide::Left);

    // Hold select
    mockController.triggerButton("select", true);

    // Simulate pad press via WindowManager
    ControllerHost::PadEvent padEvent;
    padEvent.pad = 0;
    padEvent.pressed = true;
    padEvent.pressure = 1000;

    wm.handlePadEvent(padEvent);

    // Pad 0 should be selected
    EXPECT_EQ(harness.pads().getSelectedPad(), 0);
}

// 6. Pad passthrough — simulate pad press with select=false
TEST_F(PadOverviewWidgetTests, PadPassthroughWhenSelectNotHeld)
{
    wm.setAudioEngine(&harness.audio());
    wm.setControllerHost(&mockController);

    auto widget = std::make_unique<PadOverviewWidget>();
    wm.open(std::move(widget), DisplaySide::Left);
    wm.setFocus(DisplaySide::Left);

    // Select NOT held — pad should NOT be selected
    int previousSelection = harness.pads().getSelectedPad();

    ControllerHost::PadEvent padEvent;
    padEvent.pad = 0;
    padEvent.pressed = true;
    padEvent.pressure = 1000;

    wm.handlePadEvent(padEvent);

    // Selection should not have changed
    EXPECT_EQ(harness.pads().getSelectedPad(), previousSelection);
}

// 7. Pad selected callback fires on Select+Pad
TEST_F(PadOverviewWidgetTests, PadSelectedCallbackFiresOnSelectPad)
{
    wm.setAudioEngine(&harness.audio());
    wm.setControllerHost(&mockController);

    auto sample = harness.createTemporarySampleFile("cb_test", 44100);
    harness.pads().loadSample(3, sample);

    int callbackPadIndex = -1;
    auto widget = std::make_unique<PadOverviewWidget>();
    widget->setPadSelectedCallback([&callbackPadIndex](int padIndex) {
        callbackPadIndex = padIndex;
    });
    wm.open(std::move(widget), DisplaySide::Left);
    wm.setFocus(DisplaySide::Left);

    // Hold select
    mockController.triggerButton("select", true);

    ControllerHost::PadEvent padEvent;
    padEvent.pad = 3;
    padEvent.pressed = true;
    padEvent.pressure = 1000;

    wm.handlePadEvent(padEvent);

    EXPECT_EQ(callbackPadIndex, 3);
}

TEST_F(PadOverviewWidgetTests, WindowManagerGetSideResolvesPlacement)
{
    wm.setAudioEngine(&harness.audio());
    wm.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Right);

    auto* raw = wm.getWidget(DisplaySide::Right);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(wm.getSide(raw), DisplaySide::Right);
}
