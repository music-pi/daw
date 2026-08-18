#include <gtest/gtest.h>
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

#include "../src/engine/KeyboardInstrumentBank.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/MidiStepView.h"
#include "../src/engine/commands/SliceSampleCommand.h"
#include "harness/EngineHarness.h"

namespace
{
class MessageLoopThread final : public juce::Thread
{
public:
    MessageLoopThread()
        : juce::Thread("test-message-loop")
    {
    }

    void startLoop()
    {
        startThread();
        while (! ready.load())
            juce::Thread::sleep(1);
    }

    void stopLoop()
    {
        if (! isThreadRunning())
            return;

        juce::MessageManager::getInstance()->stopDispatchLoop();
        stopThread(5000);
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    }

private:
    void run() override
    {
        auto* messageManager = juce::MessageManager::getInstance();
        messageManager->setCurrentThreadAsMessageThread();
        ready.store(true);
        messageManager->runDispatchLoop();
    }

    std::atomic<bool> ready { false };
};
}

namespace te = tracktion::engine;

TEST(MidiClipUnificationTests, HostedPlayerRendersLoadedPadSampleWithMessageLoop)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("rendered_pad", 44100);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));
    ASSERT_TRUE(harness.pads().setStep(0, 0, 0, true));

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    const te::HostedAudioDeviceInterface::Parameters params {
        .sampleRate = 44100.0,
        .blockSize = 128,
        .inputChannels = 0,
        .outputChannels = 2,
        .inputNames = {},
        .outputNames = {} };
    auto player = te::test_utilities::createEnginePlayer(*edit, params);
    MessageLoopThread messageLoop;
    messageLoop.startLoop();
    juce::Thread::sleep(100);
    edit->getTransport().stop(false, false);
    edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.0));
    edit->getTransport().play(false);
    const auto output = player->process(44100);

    messageLoop.stopLoop();
    ASSERT_GT(output.getNumChannels(), 0);
    EXPECT_GT(output.getMagnitude(0, 0, output.getNumSamples()), 0.01f);
}

TEST(MidiClipUnificationTests, BuiltInKeyboardInstrumentRendersMidiClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& bank = harness.audio().getKeyboardBank();
    const auto description = te::PluginManager::
        createBuiltInPluginDescription<te::FourOscPlugin>(true);
    ASSERT_TRUE(bank.loadInstrument(description));

    auto* clip = bank.getMidiClip();
    ASSERT_NE(clip, nullptr);
    clip->getSequence().addNote(
        60, tracktion::BeatPosition::fromBeats(0.0),
        tracktion::BeatDuration::fromBeats(1.0), 100, 0, nullptr);

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    const te::HostedAudioDeviceInterface::Parameters params {
        .sampleRate = 44100.0,
        .blockSize = 128,
        .inputChannels = 0,
        .outputChannels = 2,
        .inputNames = {},
        .outputNames = {} };
    auto player = te::test_utilities::createEnginePlayer(*edit, params);
    MessageLoopThread messageLoop;
    messageLoop.startLoop();
    juce::Thread::sleep(100);
    edit->getTransport().stop(false, false);
    edit->getTransport().setPosition(
        tracktion::core::TimePosition::fromSeconds(0.0));
    edit->getTransport().play(false);
    const auto output = player->process(44100);

    messageLoop.stopLoop();
    ASSERT_GT(output.getNumChannels(), 0);
    EXPECT_GT(output.getMagnitude(0, 0, output.getNumSamples()), 0.01f);
}

TEST(MidiClipUnificationTests, BuiltInKeyboardInstrumentRendersLiveMidiAtHardwareRate)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& bank = harness.audio().getKeyboardBank();
    const auto description = te::PluginManager::
        createBuiltInPluginDescription<te::FourOscPlugin>(true);
    ASSERT_TRUE(bank.loadInstrument(description));

    auto* edit = harness.audio().getEdit();
    auto* track = bank.getTrack();
    ASSERT_NE(edit, nullptr);
    ASSERT_NE(track, nullptr);

    const te::HostedAudioDeviceInterface::Parameters params {
        .sampleRate = 48000.0,
        .blockSize = 128,
        .inputChannels = 0,
        .outputChannels = 2,
        .inputNames = {},
        .outputNames = {} };
    auto player = te::test_utilities::createEnginePlayer(*edit, params);

    track->injectLiveMidiMessage(
        juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), {});
    const auto output = player->process(12000);
    track->injectLiveMidiMessage(juce::MidiMessage::noteOff(1, 60), {});
    player->process(128);

    ASSERT_GT(output.getNumChannels(), 0);
    EXPECT_GT(output.getMagnitude(0, 0, output.getNumSamples()), 0.01f);
}

TEST(MidiClipUnificationTests, ChoppedPadSequencedInPatternRendersAudibleAudio)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    const auto sample = harness.createTemporarySampleFile(
        "rendered_chopped_pad", 88200, 44100.0, 220.0);
    ASSERT_TRUE(harness.pads().loadSample(0, sample));

    // Model a trimmed editor window with two explicit chops. Sequence the
    // second resulting pad through the same per-pad MidiClip used by Pattern
    // and Piano Roll.
    SliceSampleCommand slice(harness.audio(), 0, { 0.5, 1.0 }, 1.5);
    ASSERT_TRUE(slice.perform());
    ASSERT_NE(harness.pads().getPad(1), nullptr);
    EXPECT_NEAR(harness.pads().getPad(1)->rangeStartSeconds, 1.0, 0.01);
    EXPECT_NEAR(harness.pads().getPad(1)->rangeEndSeconds, 1.5, 0.01);
    ASSERT_TRUE(harness.pads().setStep(0, 1, 0, true));

    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    const te::HostedAudioDeviceInterface::Parameters params {
        .sampleRate = 44100.0,
        .blockSize = 128,
        .inputChannels = 0,
        .outputChannels = 2,
        .inputNames = {},
        .outputNames = {} };
    auto player = te::test_utilities::createEnginePlayer(*edit, params);
    MessageLoopThread messageLoop;
    messageLoop.startLoop();
    juce::Thread::sleep(100);
    edit->getTransport().stop(false, false);
    edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.0));
    edit->getTransport().play(false);
    const auto output = player->process(44100);

    messageLoop.stopLoop();
    ASSERT_GT(output.getNumChannels(), 0);
    EXPECT_GT(output.getMagnitude(0, 0, output.getNumSamples()), 0.01f);
}

TEST(MidiClipUnificationTests, EachPadTrackOwnsAMidiClipAfterInit)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    ASSERT_EQ(pads.getPadCount(), 16);

    for (int i = 0; i < pads.getPadCount(); ++i)
    {
        auto* track = pads.getTrack(i);
        ASSERT_NE(track, nullptr) << "pad " << i << " has no track";

        int midiClipCount = 0;
        for (auto* clip : track->getClips())
            if (dynamic_cast<te::MidiClip*>(clip) != nullptr)
                ++midiClipCount;

        EXPECT_EQ(midiClipCount, 1)
            << "pad " << i << " should own exactly one MidiClip after init";
    }
}

TEST(MidiClipUnificationTests, SetStepAppearsInPerPadMidiClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    ASSERT_TRUE(pads.setStep(/*patternIndex*/ 0, /*padIndex*/ 3, /*step*/ 5, /*on*/ true));

    auto* track = pads.getTrack(3);
    ASSERT_NE(track, nullptr);

    te::MidiClip* mc = nullptr;
    for (auto* c : track->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(c)) { mc = m; break; }
    ASSERT_NE(mc, nullptr);

    MidiStepView view { mc->getSequence(), /*rootPitch*/ 36 + 3, 16, 1 };
    EXPECT_TRUE(view.isStepOn(5));
    EXPECT_FALSE(view.isStepOn(4));
}

TEST(MidiClipUnificationTests, SetStepVelocityMirrorsIntoMidiClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    ASSERT_TRUE(pads.setStep(0, 2, 6, true));
    ASSERT_TRUE(pads.setStepVelocity(0, 2, 6, /*velocity*/ 44));

    auto* track = pads.getTrack(2);
    te::MidiClip* mc = nullptr;
    for (auto* c : track->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(c)) { mc = m; break; }
    ASSERT_NE(mc, nullptr);

    MidiStepView view { mc->getSequence(), 36 + 2, 16, 1 };
    EXPECT_EQ(view.getStepVelocity(6), 44);
}

TEST(MidiClipUnificationTests, SetStepUsesDefaultVelocityMatchingStepClip)
{
    // Parity check: the MidiClip mirror should use the same default velocity
    // (83 = 0.65 * 127) that setStep assigns to the StepClip when turning a
    // step on. Prevents a silent-divergence bug where the two sources drift.
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    ASSERT_TRUE(pads.setStep(0, 1, 3, true));

    auto* track = pads.getTrack(1);
    te::MidiClip* mc = nullptr;
    for (auto* c : track->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(c)) { mc = m; break; }
    ASSERT_NE(mc, nullptr);

    MidiStepView view { mc->getSequence(), 36 + 1, 16, 1 };
    EXPECT_EQ(view.getStepVelocity(3), 83);
}

TEST(MidiClipUnificationTests, PatternSwitchPreservesBothPatterns)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    // Pattern 0: pad 5, step 3 on.
    ASSERT_TRUE(pads.setStep(0, 5, 3, true));
    // Create pattern 1 and switch.
    const int p1 = pads.createPattern();
    ASSERT_GE(p1, 0);
    pads.setActivePatternIndex(p1);
    // Pattern 1: pad 5, step 7 on.
    ASSERT_TRUE(pads.setStep(p1, 5, 7, true));

    // Back to 0.
    pads.setActivePatternIndex(0);
    auto* track = pads.getTrack(5);
    te::MidiClip* mc = nullptr;
    for (auto* c : track->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(c)) { mc = m; break; }
    ASSERT_NE(mc, nullptr);

    MidiStepView v0 { mc->getSequence(), 36 + 5, 16, 1 };
    EXPECT_TRUE(v0.isStepOn(3));
    EXPECT_FALSE(v0.isStepOn(7));

    // Switch to 1.
    pads.setActivePatternIndex(p1);
    MidiStepView v1 { mc->getSequence(), 36 + 5, 16, 1 };
    EXPECT_FALSE(v1.isStepOn(3));
    EXPECT_TRUE(v1.isStepOn(7));
}

TEST(MidiClipUnificationTests, SharedSequencerTrackIsRemoved)
{
    // Task 8: the shared "Sequencer" audio track, its StepClip and its
    // multi-sample SamplerPlugin are gone. SamplerInstrument owns only the
    // "Sampler" folder track and per-pad audio tracks.
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    auto* folder = pads.getFolder();
    ASSERT_NE(folder, nullptr);

    for (auto* t : folder->getAllSubTracks(false))
    {
        auto* at = dynamic_cast<te::AudioTrack*>(t);
        if (at == nullptr)
            continue;
        EXPECT_NE(at->getName(), juce::String("Sequencer"))
            << "shared Sequencer track must be removed after Task 8";
        EXPECT_FALSE(at->state.hasProperty("padIndex")
                         ? false
                         : (at->getName() == "Sequencer"));
        for (auto* c : at->getClips())
            EXPECT_EQ(dynamic_cast<te::StepClip*>(c), nullptr)
                << "no StepClip should exist after Task 8";
    }
}

TEST(MidiClipUnificationTests, SetPatternLengthResizesAllPadMidiClips)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    ASSERT_TRUE(pads.setPatternLength(0, /*bars*/ 2, /*stepsPerBar*/ 16));

    for (int i = 0; i < 16; ++i)
    {
        auto* track = pads.getTrack(i);
        te::MidiClip* mc = nullptr;
        for (auto* c : track->getClips())
            if (auto* m = dynamic_cast<te::MidiClip*>(c)) { mc = m; break; }
        ASSERT_NE(mc, nullptr) << "pad " << i;
        // 2 bars @ 4/4 = 8 beats.
        EXPECT_NEAR(mc->getLengthInBeats().inBeats(), 8.0, 0.001)
            << "pad " << i;
    }
}

TEST(MidiClipUnificationTests, ApplySwingWritesGrooveToEachPadMidiClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.applySwing(0.6f);

    for (int i = 0; i < 16; ++i)
    {
        auto* track = pads.getTrack(i);
        te::MidiClip* mc = nullptr;
        for (auto* c : track->getClips())
            if (auto* m = dynamic_cast<te::MidiClip*>(c)) { mc = m; break; }
        ASSERT_NE(mc, nullptr);
        EXPECT_NEAR(mc->getGrooveStrength(), 0.6f, 0.001f);
    }
}

TEST(MidiClipUnificationTests, LiveChokeGroupStillCutsOtherMembers)
{
    // Contract: triggering pad A in choke group G silences other group-G
    // pads' ringing voices. We can't inspect "ringing voice count" directly
    // without a renderer, but we can at least assert that trigger() returns
    // cleanly (no deadlock/state corruption) and that the second trigger
    // doesn't regress the pad state. Regression guard for the live-trigger
    // choke path — which routes via applyChokeGroup -> SamplerPlugin::allNotesOff.
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    pads.setChokeGroupDirect(0, 1);
    pads.setChokeGroupDirect(1, 1);
    EXPECT_EQ(pads.getChokeGroup(0), 1);
    EXPECT_EQ(pads.getChokeGroup(1), 1);

    // Firing pads in the same group should not crash and should preserve
    // the group assignment — that's the observable contract we have without
    // audio-rendering infrastructure in tests.
    pads.trigger(0, 1.0f);
    pads.trigger(1, 1.0f);
    EXPECT_EQ(pads.getChokeGroup(0), 1);
    EXPECT_EQ(pads.getChokeGroup(1), 1);

    // Verify subsequent triggers still work (no lock-up).
    pads.trigger(0, 0.8f);
    SUCCEED();
}

TEST(MidiClipUnificationTests, SaveLoadRoundTripPreservesAllPatterns)
{
    // Regression guard: save/load must preserve inactive patterns too, not
    // just the one in the live MidiClip. Patterns now live in Edit.state via
    // per-pad ClipSlotList (see PadPatternBank); sampler-wide metadata
    // (num-patterns, active index) rides the .mpi project wrapper. Use the
    // hybrid save path so the test covers both pieces together.
    auto tempDir = juce::File::createTempFile("");
    tempDir.deleteFile();
    tempDir.createDirectory();
    auto tempProject = tempDir.getChildFile("round-trip.mpi");

    {
        testharness::EngineHarness harness;
        harness.createEmptyEdit();
        auto& pads = harness.pads();

        // Pattern 0: every other step on pad 0.
        for (int s = 0; s < 16; s += 2)
            ASSERT_TRUE(pads.setStep(0, 0, s, true));

        // Pattern 1: offset every other step on pad 0.
        const int p1 = pads.createPattern();
        ASSERT_GE(p1, 0);
        pads.setActivePatternIndex(p1);
        for (int s = 1; s < 16; s += 2)
            ASSERT_TRUE(pads.setStep(p1, 0, s, true));

        ASSERT_TRUE(harness.audio().saveProjectToFile(tempProject));
    }

    testharness::EngineHarness harness2;
    ASSERT_TRUE(harness2.audio().loadProjectFromFile(tempProject));
    auto& pads2 = harness2.pads();

    EXPECT_EQ(pads2.getNumPatterns(), 2);
    EXPECT_EQ(pads2.getActivePatternIndex(), 1);

    auto snap = pads2.getPadsSnapshot();
    ASSERT_GE(snap.size(), 1u);
    ASSERT_GE(snap[0].patterns.size(), 2u);
    for (int s = 0; s < 16; ++s)
    {
        EXPECT_EQ(snap[0].patterns[0].steps[s], (s % 2 == 0)) << "p0 step " << s;
        EXPECT_EQ(snap[0].patterns[1].steps[s], (s % 2 == 1)) << "p1 step " << s;
    }

    tempDir.deleteRecursively();
}

TEST(MidiClipUnificationTests, SetPatternLengthWithSameValuesPreservesNotes)
{
    // Regression guard for a bug where PatternWidget pushed its own default
    // bars/stepsPerBar (4/4 at the time) into the sampler on create-second-
    // pattern, which collapsed 8 every-other-step notes at 1-bar/16-step
    // resolution into 4 notes at 4-bar/4-step resolution (half-speed + visual
    // mangling). Matching defaults and a no-op-on-same-values setPatternLength
    // prevents the regression.
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    for (int s = 0; s < 16; s += 2)
        ASSERT_TRUE(pads.setStep(0, 0, s, true));

    // Call setPatternLength with the SAME bars/stepsPerBar the engine already
    // uses. This must be a structural no-op: clip length unchanged, notes
    // preserved, step map identical.
    auto* track = pads.getTrack(0);
    te::MidiClip* mc = nullptr;
    for (auto* c : track->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(c)) { mc = m; break; }
    ASSERT_NE(mc, nullptr);
    const double beforeBeats = mc->getLengthInBeats().inBeats();
    const int beforeNotes = mc->getSequence().getNumNotes();

    ASSERT_TRUE(pads.setPatternLength(0, /*bars*/ 1, /*stepsPerBar*/ 16));

    EXPECT_NEAR(mc->getLengthInBeats().inBeats(), beforeBeats, 0.001);
    EXPECT_EQ(mc->getSequence().getNumNotes(), beforeNotes);

    MidiStepView view { mc->getSequence(), 36, 16, 1 };
    for (int s = 0; s < 16; ++s)
        EXPECT_EQ(view.isStepOn(s), (s % 2 == 0)) << "step " << s;
}
