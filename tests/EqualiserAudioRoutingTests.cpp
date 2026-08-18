#include <gtest/gtest.h>

#include <atomic>

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

#include "../src/engine/AudioEngine.h"
#include "../src/engine/ChannelInsertUtils.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/commands/InsertBuiltInPluginCommand.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

namespace
{
class EqMessageLoopThread final : public juce::Thread
{
public:
    EqMessageLoopThread() : juce::Thread("eq-routing-message-loop") {}
    ~EqMessageLoopThread() override { stopLoop(); }

    void startLoop()
    {
        startThread();
        while (!ready_.load())
            juce::Thread::sleep(1);
    }

    void stopLoop()
    {
        if (!isThreadRunning())
            return;
        juce::MessageManager::getInstance()->stopDispatchLoop();
        stopThread(5000);
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    }

private:
    void run() override
    {
        auto* manager = juce::MessageManager::getInstance();
        manager->setCurrentThreadAsMessageThread();
        ready_.store(true);
        manager->runDispatchLoop();
    }

    std::atomic<bool> ready_ { false };
};

float processRms(te::Edit& edit, te::test_utilities::EnginePlayer& player)
{
    juce::Thread::sleep(100);
    edit.getTransport().stop(false, false);
    edit.getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.0));
    edit.getTransport().play(false);
    const auto output = player.process(44100);
    edit.getTransport().stop(false, false);
    if (output.getNumChannels() == 0 || output.getNumSamples() == 0)
        return 0.0f;
    return output.getRMSLevel(0, 0, output.getNumSamples());
}

enum class RoutingTarget { Pad, Group, Master };

te::PluginList& targetList(testharness::EngineHarness& harness, RoutingTarget target)
{
    switch (target)
    {
        case RoutingTarget::Pad:
            return harness.pads().getTrack(0)->pluginList;
        case RoutingTarget::Group:
            return harness.pads().getFolder()->pluginList;
        case RoutingTarget::Master:
            return harness.audio().getEdit()->getMasterPluginList();
    }
    return harness.audio().getEdit()->getMasterPluginList();
}

void verifyTarget(RoutingTarget target)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto sample = harness.createTemporarySampleFile(
        "eq-routing-tone", 44100, 44100.0, 1000.0);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));
    ASSERT_TRUE(harness.pads().setStep(0, 0, 0, true));

    auto& list = targetList(harness, target);
    const int insertAt = ChannelInsertUtils::insertionIndex(list);
    auto& undo = harness.audio().getUndoManager();
    undo.beginNewTransaction();
    ASSERT_TRUE(undo.perform(new InsertBuiltInPluginCommand(
        harness.audio(), list, te::EqualiserPlugin::xmlTypeName, insertAt)));

    auto* equaliser = ChannelInsertUtils::findEqualiser(list);
    ASSERT_NE(equaliser, nullptr);
    equaliser->setMidFreq1(1000.0f);
    equaliser->setMidQ1(4.0f);
    equaliser->setMidGain1(-20.0f);

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    const te::HostedAudioDeviceInterface::Parameters parameters {
        .sampleRate = 44100.0,
        .blockSize = 128,
        .inputChannels = 0,
        .outputChannels = 2,
        .inputNames = {},
        .outputNames = {}
    };
    auto player = te::test_utilities::createEnginePlayer(*edit, parameters);
    EqMessageLoopThread messageLoop;
    messageLoop.startLoop();
    const float filteredRms = processRms(*edit, *player);
    EXPECT_GT(filteredRms, 0.0001f);
    EXPECT_LT(filteredRms, 0.01f)
        << "EQ did not attenuate the 1 kHz reference at its owning hierarchy level";

    player.reset();
    messageLoop.stopLoop();
}
}

TEST(EqualiserAudioRoutingTests, NativeEqProcessesPadAudioNode)
{
    verifyTarget(RoutingTarget::Pad);
}

TEST(EqualiserAudioRoutingTests, DryReferenceIsWellAboveFilteredThreshold)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    const auto sample = harness.createTemporarySampleFile(
        "eq-routing-dry-tone", 44100, 44100.0, 1000.0);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));
    ASSERT_TRUE(harness.pads().setStep(0, 0, 0, true));

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    const te::HostedAudioDeviceInterface::Parameters parameters {
        .sampleRate = 44100.0,
        .blockSize = 128,
        .inputChannels = 0,
        .outputChannels = 2,
        .inputNames = {},
        .outputNames = {}
    };
    auto player = te::test_utilities::createEnginePlayer(*edit, parameters);
    EqMessageLoopThread messageLoop;
    messageLoop.startLoop();
    const float dryRms = processRms(*edit, *player);
    EXPECT_GT(dryRms, 0.04f);
    player.reset();
    messageLoop.stopLoop();
}

TEST(EqualiserAudioRoutingTests, NativeEqProcessesGroupAudioNode)
{
    verifyTarget(RoutingTarget::Group);
}

TEST(EqualiserAudioRoutingTests, NativeEqProcessesMasterAudioNode)
{
    verifyTarget(RoutingTarget::Master);
}
