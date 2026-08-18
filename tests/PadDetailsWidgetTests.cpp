#include <gtest/gtest.h>

#include "../src/ui/widget/PadDetailsWidget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

class PadDetailsWidgetTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    InputManager inputManager;
    testharness::MockControllerHost mockController;
    WindowManager wm;
};

// 1. Descriptor
TEST_F(PadDetailsWidgetTests, DescriptorIsCorrect)
{
    PadDetailsWidget widget;
    auto desc = widget.describe();

    EXPECT_EQ(desc.id, "pad_details");
    EXPECT_EQ(desc.pageCount, 2);
    EXPECT_FALSE(desc.forceOnTop);
    EXPECT_EQ(desc.display, DisplayConstraint::Any);
}

// 2. Resources -- panel options + knobs only, no pads
TEST_F(PadDetailsWidgetTests, ResourcesLeftPanel)
{
    auto widget = std::make_unique<PadDetailsWidget>();
    auto* raw = widget.get();

    // Default panelOffset is 0 (left)
    auto resources = raw->requiredResources(0);

    // Should have d1-d4, k1-k4
    for (int i = 1; i <= 4; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Missing k" << i;
    }

    // Should NOT have pad LEDs
    for (int i = 1; i <= 16; ++i)
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "p" + std::to_string(i)), resources.end())
            << "Should not have p" << i;

    // Should NOT have right panel resources
    for (int i = 5; i <= 8; ++i)
    {
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Should not have d" << i;
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Should not have k" << i;
    }
}

TEST_F(PadDetailsWidgetTests, ResourcesRightPanel)
{
    wm.setAudioEngine(&harness.audio());

    auto widget = std::make_unique<PadDetailsWidget>();
    wm.open(std::move(widget), DisplaySide::Right);

    auto* raw = static_cast<PadDetailsWidget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(raw, nullptr);

    auto resources = raw->requiredResources(0);

    // Right panel: d5-d8, k5-k8
    for (int i = 5; i <= 8; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Missing k" << i;
    }

    // Should NOT have left panel resources
    for (int i = 1; i <= 4; ++i)
    {
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Should not have d" << i;
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Should not have k" << i;
    }
}

// 3. Choke group change is undoable
TEST_F(PadDetailsWidgetTests, ChokeGroupChangeIsUndoable)
{
    wm.setAudioEngine(&harness.audio());

    // Load a sample and select the pad
    auto sample = harness.createTemporarySampleFile("choke_test", 44100);
    harness.pads().loadSample(0, sample);
    harness.pads().selectPad(0);

    auto widget = std::make_unique<PadDetailsWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    // Get knobs and verify choke knob is present
    auto* raw = static_cast<PadDetailsWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto knobs = raw->getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[3].id, "padChokeGroup");
    EXPECT_TRUE(knobs[3].isEnabled);

    // Verify initial choke group is 0
    EXPECT_EQ(harness.pads().getChokeGroup(0), 0);

    // Change choke group via the knob's onChange
    auto* listModel = std::get_if<Knob::ListModel>(&knobs[3].model);
    ASSERT_NE(listModel, nullptr);
    // Index 0=None, 1=Self, 2=Group 1, 3=Group 2, ..., 9=Group 8
    listModel->onChange(3);  // Set to "Group 2"

    EXPECT_EQ(harness.pads().getChokeGroup(0), 2);

    // Undo should restore the original value
    harness.audio().getUndoManager().undo();
    EXPECT_EQ(harness.pads().getChokeGroup(0), 0);
}

// 4. Lifecycle -- activation starts timer, deactivation stops
TEST_F(PadDetailsWidgetTests, LifecycleActivationDeactivation)
{
    wm.setAudioEngine(&harness.audio());

    // Select a pad so widget has something to show
    auto sample = harness.createTemporarySampleFile("lifecycle_test", 44100);
    harness.pads().loadSample(0, sample);
    harness.pads().selectPad(0);

    auto widget = std::make_unique<PadDetailsWidget>();
    wm.open(std::move(widget), DisplaySide::Left);

    auto& hw = wm.getHardwareState();

    // After opening, option/knob resources should be claimed
    EXPECT_TRUE(hw.isClaimed("d1"));
    EXPECT_TRUE(hw.isClaimed("k1"));
    EXPECT_EQ(hw.getOwner("d1"), "pad_details");

    // Pad LEDs should NOT be claimed by this widget
    EXPECT_NE(hw.getOwner("p1"), "pad_details");

    // Close widget
    wm.close("pad_details");

    // Resources should be released
    EXPECT_FALSE(hw.isClaimed("d1"));
    EXPECT_FALSE(hw.isClaimed("k1"));
}

TEST_F(PadDetailsWidgetTests, TriggerModeOptionCyclesAllModes)
{
    wm.setAudioEngine(&harness.audio());
    const auto sample = harness.createTemporarySampleFile("trigger_ui", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));
    harness.pads().selectPad(0);

    wm.open(std::make_unique<PadDetailsWidget>(), DisplaySide::Left);
    auto* widget = static_cast<PadDetailsWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(widget, nullptr);

    const std::array expected {
        SamplerInstrument::TriggerMode::OneShot,
        SamplerInstrument::TriggerMode::VelocityRoundRobinOrdered,
        SamplerInstrument::TriggerMode::VelocityRoundRobinRandom,
        SamplerInstrument::TriggerMode::HoldEnvelope
    };
    for (const auto mode : expected)
    {
        auto options = widget->getOptions(0);
        ASSERT_EQ(options.size(), 4u);
        ASSERT_EQ(options[2].id, "pad.triggerMode");
        ASSERT_TRUE(options[2].onInvoke);
        options[2].onInvoke();
        EXPECT_EQ(harness.pads().getTriggerMode(0), mode);
    }
}

TEST_F(PadDetailsWidgetTests, LayeredPadCyclesOnlyLayerRoundRobinModes)
{
    wm.setAudioEngine(&harness.audio());
    const auto first = harness.createTemporarySampleFile("layer_mode_first", 44100);
    const auto second = harness.createTemporarySampleFile("layer_mode_second", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, first));
    ASSERT_TRUE(harness.pads().addSampleLayer(0, second));
    harness.pads().setTriggerMode(
        0, SamplerInstrument::TriggerMode::HoldEnvelope);
    harness.pads().selectPad(0);

    wm.open(std::make_unique<PadDetailsWidget>(), DisplaySide::Left);
    auto* widget = static_cast<PadDetailsWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(widget, nullptr);

    const std::array expected {
        SamplerInstrument::TriggerMode::OneShot,
        SamplerInstrument::TriggerMode::RoundRobinOrdered,
        SamplerInstrument::TriggerMode::RoundRobinRandom,
        SamplerInstrument::TriggerMode::HoldEnvelope
    };
    for (const auto mode : expected)
    {
        auto options = widget->getOptions(0);
        options[2].onInvoke();
        EXPECT_EQ(harness.pads().getTriggerMode(0), mode);
    }
}

TEST_F(PadDetailsWidgetTests, SingleLayerPageExposesVelocityRangeInsteadOfLayerWeight)
{
    wm.setAudioEngine(&harness.audio());
    ASSERT_TRUE(harness.pads().loadSample(
        0, harness.createTemporarySampleFile("velocity_range_ui", 44100)));
    harness.pads().selectPad(0);

    wm.open(std::make_unique<PadDetailsWidget>(), DisplaySide::Left);
    auto* widget = static_cast<PadDetailsWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(widget, nullptr);
    auto knobs = widget->getKnobs(1);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[0].id, "pad.layer.curve");
    EXPECT_EQ(knobs[1].id, "pad.layer.gain");
    EXPECT_EQ(knobs[2].id, "pad.layer.velocityMinimum");
    EXPECT_EQ(knobs[3].id, "pad.layer.velocityMaximum");

    auto* minimum = std::get_if<Knob::NumericModel>(&knobs[2].model);
    auto* maximum = std::get_if<Knob::NumericModel>(&knobs[3].model);
    ASSERT_NE(minimum, nullptr);
    ASSERT_NE(maximum, nullptr);
    minimum->onChange(60.0);
    knobs = widget->getKnobs(1);
    maximum = std::get_if<Knob::NumericModel>(&knobs[3].model);
    ASSERT_NE(maximum, nullptr);
    maximum->onChange(90.0);

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_EQ(pad->sampleLayers.size(), 1u);
    EXPECT_NEAR(pad->sampleLayers[0].velocityMinimum, 0.6f, 0.001f);
    EXPECT_NEAR(pad->sampleLayers[0].velocityMaximum, 0.9f, 0.001f);
}

TEST_F(PadDetailsWidgetTests, LayerPageEditsSelectedLayerMix)
{
    wm.setAudioEngine(&harness.audio());
    const auto first = harness.createTemporarySampleFile("layer_ui_first", 44100);
    const auto second = harness.createTemporarySampleFile("layer_ui_second", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, first));
    ASSERT_TRUE(harness.pads().addSampleLayer(0, second));
    harness.pads().selectPad(0);

    wm.open(std::make_unique<PadDetailsWidget>(), DisplaySide::Left);
    auto* widget = static_cast<PadDetailsWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(widget, nullptr);

    auto knobs = widget->getKnobs(1);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[0].id, "pad.layer.select");
    EXPECT_EQ(knobs[1].id, "pad.layer.gain");
    EXPECT_EQ(knobs[2].id, "pad.layer.weight");
    EXPECT_EQ(knobs[3].id, "pad.layer.curve");

    auto* layerModel = std::get_if<Knob::ListModel>(&knobs[0].model);
    ASSERT_NE(layerModel, nullptr);
    ASSERT_EQ(layerModel->entries.size(), 2u);
    layerModel->onChange(1);

    knobs = widget->getKnobs(1);
    auto* gainModel = std::get_if<Knob::NumericModel>(&knobs[1].model);
    auto* weightModel = std::get_if<Knob::NumericModel>(&knobs[2].model);
    auto* curveModel = std::get_if<Knob::ListModel>(&knobs[3].model);
    ASSERT_NE(gainModel, nullptr);
    ASSERT_NE(weightModel, nullptr);
    ASSERT_NE(curveModel, nullptr);
    gainModel->onChange(-7.5);
    weightModel->onChange(35.0);
    curveModel->onChange(
        static_cast<int>(SamplerInstrument::LayerVelocityCurve::Hard));

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_EQ(pad->sampleLayers.size(), 2u);
    EXPECT_NEAR(pad->sampleLayers[1].gainDb, -7.5f, 0.001f);
    EXPECT_NEAR(pad->sampleLayers[1].randomWeight, 0.35f, 0.001f);
    EXPECT_EQ(pad->sampleLayers[1].velocityCurve,
              SamplerInstrument::LayerVelocityCurve::Hard);
}

TEST_F(PadDetailsWidgetTests, ArrowLedsShowAvailableLayerPageNavigation)
{
    wm.setAudioEngine(&harness.audio());
    wm.open(std::make_unique<PadDetailsWidget>(), DisplaySide::Left);

    const auto& hardware = wm.getHardwareState();
    EXPECT_EQ(hardware.getOwner("arrowLeft"), "pad_details");
    EXPECT_EQ(hardware.getOwner("arrowRight"), "pad_details");
    EXPECT_EQ(hardware.getLed("arrowLeft"), 0);
    EXPECT_GT(hardware.getLed("arrowRight"), 0);

    ControllerHost::ButtonEvent right;
    right.name = "arrowRight";
    right.pressed = true;
    wm.handleButtonEvent(right);

    EXPECT_EQ(wm.pageForPanel(DisplaySide::Left), 1);
    EXPECT_GT(hardware.getLed("arrowLeft"), 0);
    EXPECT_EQ(hardware.getLed("arrowRight"), 0);

    ControllerHost::ButtonEvent left;
    left.name = "arrowLeft";
    left.pressed = true;
    wm.handleButtonEvent(left);

    EXPECT_EQ(wm.pageForPanel(DisplaySide::Left), 0);
    EXPECT_EQ(hardware.getLed("arrowLeft"), 0);
    EXPECT_GT(hardware.getLed("arrowRight"), 0);
}

TEST_F(PadDetailsWidgetTests, LayerToPadsLightsAndAuditionsWithoutPressureRetrigger)
{
    struct TriggerCounter final : SamplerInstrument::Listener
    {
        explicit TriggerCounter(SamplerInstrument& samplerIn)
            : sampler(samplerIn)
        {
            sampler.addListener(this);
        }
        ~TriggerCounter() override { sampler.removeListener(this); }
        void padTriggered(int) override { ++count; }
        SamplerInstrument& sampler;
        int count { 0 };
    } counter(harness.pads());

    wm.setAudioEngine(&harness.audio());
    mockController.setInputManager(&inputManager);
    wm.setControllerHost(&mockController);
    const auto first = harness.createTemporarySampleFile("pad_layer_first", 44100);
    const auto second = harness.createTemporarySampleFile("pad_layer_second", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, first));
    ASSERT_TRUE(harness.pads().addSampleLayer(0, second));
    harness.pads().selectPad(0);

    wm.open(std::make_unique<PadDetailsWidget>(), DisplaySide::Left);
    auto* widget = static_cast<PadDetailsWidget*>(wm.getWidget(DisplaySide::Left));
    ASSERT_NE(widget, nullptr);

    auto options = widget->getOptions(0);
    ASSERT_EQ(options.size(), 4u);
    EXPECT_EQ(options[3].id, "pad.layerToPads");
    EXPECT_EQ(options[3].state, OptionState::Enabled);
    ASSERT_TRUE(options[3].onInvoke);
    options[3].onInvoke();

    options = widget->getOptions(0);
    EXPECT_EQ(options[3].state, OptionState::Active);
    const auto& hardware = wm.getHardwareState();
    EXPECT_EQ(hardware.getOwner("p1"), "pad_details");
    EXPECT_EQ(hardware.getOwner("p2"), "pad_details");
    EXPECT_GT(hardware.getLed("p1"), 0);
    EXPECT_GT(hardware.getLed("p2"), 0);
    EXPECT_EQ(hardware.getLed("p3"), 0);

    auto* input = mockController.getInputManager();
    ASSERT_NE(input, nullptr);
    auto press = InputEvent::makePad(0, true, 900, false, "test");
    input->dispatch(press);
    EXPECT_TRUE(press.consumed);
    EXPECT_EQ(counter.count, 1);
    const auto pressedColour = hardware.getLed("p1");

    // A pressure update with the pressed bit still set must not audition again.
    auto pressure = InputEvent::makePad(0, true, 3000, false, "test");
    input->dispatch(pressure);
    EXPECT_TRUE(pressure.consumed);
    EXPECT_EQ(counter.count, 1);
    EXPECT_EQ(hardware.getLed("p1"), pressedColour);

    auto release = InputEvent::makePad(0, false, 0, false, "test");
    input->dispatch(release);
    EXPECT_TRUE(release.consumed);

    auto secondPress = InputEvent::makePad(0, true, 900, false, "test");
    input->dispatch(secondPress);
    EXPECT_EQ(counter.count, 2);

    options = widget->getOptions(0);
    options[3].onInvoke();
    EXPECT_FALSE(hardware.isClaimed("p1"));
    EXPECT_FALSE(hardware.isClaimed("p2"));
}
