#include <gtest/gtest.h>

#include "../src/ui/widget/PatternWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/KeyboardInstrumentBank.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/commands/ToggleSequencerStepCommand.h"
#include "../src/ui/theme/UiTheme.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

class PatternWidgetTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        wm.setAudioEngine(&harness.audio());
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost mockController;
    WindowManager wm;
};

// 1. Descriptor -- id, pageCount
TEST_F(PatternWidgetTests, DescriptorIsCorrect)
{
    PatternWidget widget;
    auto desc = widget.describe();

    EXPECT_EQ(desc.id, "pattern");
    EXPECT_EQ(desc.pageCount, 1);
    EXPECT_FALSE(desc.forceOnTop);
    EXPECT_EQ(desc.display, DisplayConstraint::Any);
}

TEST_F(PatternWidgetTests, KeyboardModeUsesD3ForPianoRoll)
{
    const auto description = tracktion::engine::PluginManager::
        createBuiltInPluginDescription<tracktion::engine::FourOscPlugin>(true);
    ASSERT_TRUE(harness.audio().getKeyboardBank().loadInstrument(description));

    auto widget = std::make_unique<PatternWidget>();
    auto* raw = widget.get();
    bool requested = false;
    raw->setOnPianoRollRequested([&requested]() { requested = true; });
    wm.open(std::move(widget), DisplaySide::Left);
    raw->setChannelMode(PatternWidget::ChannelMode::Instruments);

    auto options = raw->getOptions(0);
    ASSERT_EQ(options.size(), 3u);
    EXPECT_EQ(options[1].id, "pattern.piano_roll");
    EXPECT_EQ(options[1].label, "Piano Roll");
    ASSERT_TRUE(static_cast<bool>(options[1].onInvoke));
    options[1].onInvoke();
    EXPECT_TRUE(requested);
}

// 2. Resource declaration -- returns pad LEDs, step button, and panel option/knob IDs
TEST_F(PatternWidgetTests, ResourceDeclarationLeftPanel)
{
    auto widget = std::make_unique<PatternWidget>();
    auto* raw = widget.get();

    // Default panelOffset is 0 (left)
    auto resources = raw->requiredResources(0);

    // Should have pad LEDs p1-p16
    for (int i = 1; i <= 16; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "p" + std::to_string(i)), resources.end())
            << "Missing p" << i;
    }

    // Should have step button
    EXPECT_NE(std::find(resources.begin(), resources.end(), "step"), resources.end());

    // Should have d1-d4, k1-k4
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

TEST_F(PatternWidgetTests, ResourceDeclarationRightPanel)
{
    // Open on Right side to get panelOffset=1
    auto widget = std::make_unique<PatternWidget>();
    wm.open(std::move(widget), DisplaySide::Right);

    auto* raw = static_cast<PatternWidget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(raw, nullptr);

    auto resources = raw->requiredResources(0);

    // Pad LEDs p1-p16 (shared, regardless of panel)
    for (int i = 1; i <= 16; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "p" + std::to_string(i)), resources.end())
            << "Missing p" << i;
    }

    // Right panel: d5-d8, k5-k8
    for (int i = 5; i <= 8; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Missing k" << i;
    }
}

// 3. Step toggle creates undoable action
TEST_F(PatternWidgetTests, StepToggleCreatesUndoableAction)
{
    // Load a sample so there's a channel to select
    auto sample = harness.createTemporarySampleFile("pattern_test", 44100);
    harness.pads().loadSample(0, sample);

    auto widget = std::make_unique<PatternWidget>();
    wm.open(std::move(widget), DisplaySide::Left);
    wm.setFocus(DisplaySide::Left);

    auto* raw = static_cast<PatternWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    // Step mode auto-selects first channel when samples are loaded
    EXPECT_GE(raw->getSelectedChannelPadId(), 0);
    EXPECT_GE(raw->getSelectedPatternIndex(), 0);

    // Get initial step state
    auto snapshots = harness.pads().getPadsSnapshot();
    ASSERT_FALSE(snapshots.empty());
    ASSERT_FALSE(snapshots[0].patterns.empty());
    bool initialStep0 = snapshots[0].patterns[0].steps[0];

    // Toggle step 0 using ToggleSequencerStepCommand directly so this test can
    // inspect the command's undo behavior independently of input routing.
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto& um = edit->getUndoManager();
    um.beginNewTransaction("Toggle Step");
    um.perform(new ToggleSequencerStepCommand(harness.audio(), raw->getSelectedPatternIndex(),
                                               raw->getSelectedChannelPadId(), 0, !initialStep0));

    // Verify step was toggled
    auto snapshotsAfter = harness.pads().getPadsSnapshot();
    ASSERT_FALSE(snapshotsAfter.empty());
    ASSERT_FALSE(snapshotsAfter[0].patterns.empty());
    bool afterStep0 = snapshotsAfter[0].patterns[0].steps[0];
    EXPECT_NE(initialStep0, afterStep0) << "Step should have been toggled";

    // Verify it's undoable
    EXPECT_TRUE(harness.audio().canUndo());
    harness.audio().undo();

    // Step should be back to initial state
    auto snapshotsUndone = harness.pads().getPadsSnapshot();
    ASSERT_FALSE(snapshotsUndone.empty());
    ASSERT_FALSE(snapshotsUndone[0].patterns.empty());
    bool undoneStep0 = snapshotsUndone[0].patterns[0].steps[0];
    EXPECT_EQ(initialStep0, undoneStep0) << "Step should be back to initial state after undo";
}

// 4. Channel navigation
TEST_F(PatternWidgetTests, ChannelNavigationWraps)
{
    // Load two samples
    auto sample1 = harness.createTemporarySampleFile("nav_test1", 44100);
    auto sample2 = harness.createTemporarySampleFile("nav_test2", 44100);
    harness.pads().loadSample(0, sample1);
    harness.pads().loadSample(1, sample2);

    auto widget = std::make_unique<PatternWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto* raw = static_cast<PatternWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    // Step mode auto-selects first channel when samples are loaded
    int firstPadId = raw->getSelectedChannelPadId();
    EXPECT_GE(firstPadId, 0);

    // Navigate down
    raw->navigateChannel(1);
    int secondPadId = raw->getSelectedChannelPadId();
    EXPECT_NE(firstPadId, secondPadId) << "Should have navigated to different channel";

    // Navigate down again (should wrap back to first)
    raw->navigateChannel(1);
    EXPECT_EQ(raw->getSelectedChannelPadId(), firstPadId) << "Should wrap back to first channel";
}

// 5. Lifecycle (activate/deactivate)
TEST_F(PatternWidgetTests, LifecycleActivateDeactivate)
{
    auto widget = std::make_unique<PatternWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto& hw = wm.getHardwareState();

    // After opening, pad LEDs, step button, and panel options/knobs should be claimed
    EXPECT_TRUE(hw.isClaimed("d1"));
    EXPECT_TRUE(hw.isClaimed("k1"));
    EXPECT_TRUE(hw.isClaimed("p1"));
    EXPECT_TRUE(hw.isClaimed("step"));
    EXPECT_EQ(hw.getOwner("d1"), "pattern");
    EXPECT_EQ(hw.getOwner("p1"), "pattern");

    // Close the widget
    wm.close("pattern");

    // Resources should be released
    EXPECT_FALSE(hw.isClaimed("d1"));
    EXPECT_FALSE(hw.isClaimed("k1"));
    EXPECT_FALSE(hw.isClaimed("p1"));
    EXPECT_FALSE(hw.isClaimed("step"));
}

TEST_F(PatternWidgetTests, NonStepModeLightsPopulatedPads)
{
    auto sample = harness.createTemporarySampleFile("pad_led_test", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));
    ASSERT_TRUE(harness.pads().loadSample(4, sample));

    wm.open(std::make_unique<PatternWidget>(), DisplaySide::Left);

    auto& hw = wm.getHardwareState();
    EXPECT_FALSE(static_cast<PatternWidget*>(wm.getWidget(DisplaySide::Left))
                     ->isStepModeActive());
    EXPECT_NE(hw.getLed("p1"), 0);
    EXPECT_EQ(hw.getLed("p2"), 0);
    EXPECT_NE(hw.getLed("p5"), 0);
}
