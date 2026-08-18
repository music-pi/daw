/**
 * QA Feature Tests — exercises features 1-12 from QA-REGISTRY.md
 *
 * These tests verify core engine, audio operation, and group management
 * features that don't require a running GUI.
 */
#include <gtest/gtest.h>

#include "../src/engine/Arpeggiator.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/KeyboardInstrumentBank.h"
#include "../src/engine/RoundRobinMidiPlugin.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/engine/GroupManager.h"

#include "harness/EngineHarness.h"

namespace
{
juce::File makeQaTempFile(const juce::String& stem, const juce::String& extension)
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile(stem + "-" + juce::Uuid().toString());
    return file.withFileExtension(extension);
}
}

// ============================================================================
// Feature 1: Edit Lifecycle
// ============================================================================

TEST(QaEditLifecycle, ShutdownCleanlyWithoutCrash)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    EXPECT_TRUE(harness.audio().hasEdit());

    harness.audio().shutdown();
    EXPECT_FALSE(harness.audio().hasEdit());
    // If we reach here without crash, shutdown is clean
}

TEST(QaEditLifecycle, CreateNewEditAfterShutdown)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    EXPECT_TRUE(harness.audio().hasEdit());

    harness.audio().shutdown();
    EXPECT_FALSE(harness.audio().hasEdit());

    // Re-create
    harness.audio().createEmptyEdit();
    EXPECT_TRUE(harness.audio().hasEdit());
    EXPECT_EQ(harness.pads().getPadCount(), 16);
}

TEST(QaEditLifecycle, PanicAllNotesClearsArpeggiatorAndIsSafeWithInstruments)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& arp = harness.audio().getKeyboardArp();
    Arpeggiator::Settings settings;
    settings.mode = Arpeggiator::Mode::Up;
    arp.setSettings(settings);
    arp.noteOn(60, 100);
    ASSERT_EQ(arp.getHeldCount(), 1);

    const auto description = tracktion::engine::PluginManager::
        createBuiltInPluginDescription<tracktion::engine::FourOscPlugin>(true);
    ASSERT_TRUE(harness.audio().getKeyboardBank().loadInstrument(description));

    harness.audio().panicAllNotes();

    EXPECT_EQ(arp.getHeldCount(), 0);
}

// ============================================================================
// Feature 2: Project Save/Load
// ============================================================================

TEST(QaProjectSaveLoad, SaveAndReloadEdit)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sampleFile = harness.createTemporarySampleFile("save_test", 22050);
    ASSERT_TRUE(sampleFile.existsAsFile());
    harness.pads().loadSample(0, sampleFile);

    // Save to a temp .edit file
    auto editFile = makeQaTempFile("qa_test_save", "edit");
    editFile.deleteFile();

    bool saved = harness.audio().saveEditToFile(editFile);
    EXPECT_TRUE(saved);
    EXPECT_TRUE(editFile.existsAsFile());

    // Shutdown and reload
    harness.audio().shutdown();
    bool loaded = harness.audio().loadEditFromFile(editFile);
    EXPECT_TRUE(loaded);
    EXPECT_TRUE(harness.audio().hasEdit());

    editFile.deleteFile();
}

TEST(QaProjectSaveLoad, SaveAndReloadProject)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sampleFile = harness.createTemporarySampleFile("proj_test", 22050);
    ASSERT_TRUE(harness.pads().loadSample(0, sampleFile));
    ASSERT_TRUE(harness.pads().setStep(0, 0, 3, true));

    const int secondPattern = harness.pads().createPattern("Pattern 2");
    ASSERT_EQ(secondPattern, 1);
    harness.pads().setActivePatternIndex(secondPattern);
    ASSERT_TRUE(harness.pads().setStep(secondPattern, 0, 7, true));

    auto projFile = makeQaTempFile("qa_test_save", "mpi");
    projFile.deleteFile();

    bool saved = harness.audio().saveProjectToFile(projFile);
    EXPECT_TRUE(saved);
    EXPECT_TRUE(projFile.existsAsFile());

    const auto packagedSamples = projFile.getSiblingFile(
        projFile.getFileNameWithoutExtension() + ".samples");
    EXPECT_TRUE(packagedSamples.isDirectory());
    EXPECT_TRUE(packagedSamples.getChildFile("1-proj_test.wav").existsAsFile());
    ASSERT_TRUE(sampleFile.deleteFile());

    harness.audio().shutdown();
    bool loaded = harness.audio().loadProjectFromFile(projFile);
    EXPECT_TRUE(loaded);
    EXPECT_TRUE(harness.audio().hasEdit());

    const auto snapshots = harness.pads().getPadsSnapshot();
    ASSERT_FALSE(snapshots.empty());
    EXPECT_TRUE(snapshots[0].hasSample);
    EXPECT_EQ(snapshots[0].sampleName, "proj_test");
    EXPECT_TRUE(snapshots[0].sampleFile.existsAsFile());
    EXPECT_NE(snapshots[0].sampleFile, sampleFile);
    EXPECT_EQ(harness.pads().getNumPatterns(), 2);
    EXPECT_EQ(harness.pads().getActivePatternIndex(), 1);

    const auto findPattern = [&snapshots](int patternIndex)
        -> const SamplerInstrument::PadSnapshot::PatternSnapshot*
    {
        for (const auto& pattern : snapshots[0].patterns)
            if (pattern.patternIndex == patternIndex)
                return &pattern;
        return nullptr;
    };

    const auto* patternZero = findPattern(0);
    const auto* patternOne = findPattern(1);
    ASSERT_NE(patternZero, nullptr);
    ASSERT_NE(patternOne, nullptr);
    EXPECT_TRUE(patternZero->steps[3]);
    EXPECT_TRUE(patternOne->steps[7]);

    projFile.deleteFile();
    packagedSamples.deleteRecursively();
}

TEST(QaProjectSaveLoad, RoundRobinLayersAndMixRoundTripAcrossFreshEngine)
{
    const auto projectFile = makeQaTempFile("qa_round_robin_layers", "mpi");
    projectFile.deleteFile();
    const auto packagedSamples = projectFile.getSiblingFile(
        projectFile.getFileNameWithoutExtension() + ".samples");

    {
        testharness::EngineHarness harness;
        harness.createEmptyEdit();
        auto& pads = harness.pads();
        const auto first = harness.createTemporarySampleFile("rr_saved_first", 4410);
        const auto second = harness.createTemporarySampleFile("rr_saved_second", 8820);

        ASSERT_TRUE(pads.loadSample(0, first));
        ASSERT_TRUE(pads.addSampleLayer(0, second));
        pads.setTriggerMode(
            0, SamplerInstrument::TriggerMode::RoundRobinRandom);
        ASSERT_TRUE(pads.setSampleLayerGainDb(0, 0, -4.5f));
        ASSERT_TRUE(pads.setSampleLayerRandomWeight(0, 0, 0.25f));
        ASSERT_TRUE(pads.setSampleLayerVelocityCurve(
            0, 0, SamplerInstrument::LayerVelocityCurve::Soft));
        ASSERT_TRUE(pads.setSampleLayerVelocityRange(0, 0, 0.55f, 0.9f));
        ASSERT_TRUE(pads.setSampleLayerGainDb(0, 1, 2.0f));
        ASSERT_TRUE(pads.setSampleLayerRandomWeight(0, 1, 0.75f));
        ASSERT_TRUE(pads.setSampleLayerVelocityCurve(
            0, 1, SamplerInstrument::LayerVelocityCurve::Hard));
        ASSERT_TRUE(pads.setSampleLayerVelocityRange(0, 1, 0.7f, 1.0f));
        pads.setChokeGroupDirect(0, -1);

        ASSERT_TRUE(harness.audio().saveProjectToFile(projectFile));
    }

    testharness::EngineHarness restored;
    ASSERT_TRUE(restored.audio().loadProjectFromFile(projectFile));

    const auto* pad = restored.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(pad->sampler, nullptr);
    ASSERT_NE(pad->roundRobinMidi, nullptr);
    ASSERT_EQ(pad->sampleLayers.size(), 2u);
    EXPECT_EQ(pad->triggerMode,
              SamplerInstrument::TriggerMode::RoundRobinRandom);
    EXPECT_EQ(pad->roundRobinMidi->getPolicy(),
              RoundRobinMidiPlugin::Policy::Random);
    EXPECT_EQ(pad->roundRobinMidi->getLayerCount(), 2);
    EXPECT_EQ(pad->sampler->getNumSounds(), 2);
    EXPECT_NEAR(pad->sampleLayers[0].gainDb, -4.5f, 0.001f);
    EXPECT_NEAR(pad->sampleLayers[0].randomWeight, 0.25f, 0.001f);
    EXPECT_EQ(pad->sampleLayers[0].velocityCurve,
              SamplerInstrument::LayerVelocityCurve::Soft);
    EXPECT_NEAR(pad->sampleLayers[0].velocityMinimum, 0.55f, 0.001f);
    EXPECT_NEAR(pad->sampleLayers[0].velocityMaximum, 0.9f, 0.001f);
    EXPECT_NEAR(pad->sampleLayers[1].gainDb, 2.0f, 0.001f);
    EXPECT_NEAR(pad->sampleLayers[1].randomWeight, 0.75f, 0.001f);
    EXPECT_EQ(pad->sampleLayers[1].velocityCurve,
              SamplerInstrument::LayerVelocityCurve::Hard);
    EXPECT_NEAR(pad->sampleLayers[1].velocityMinimum, 0.7f, 0.001f);
    EXPECT_NEAR(pad->sampleLayers[1].velocityMaximum, 1.0f, 0.001f);
    EXPECT_EQ(restored.pads().getChokeGroup(0), -1);
    EXPECT_TRUE(pad->sampleLayers[0].file.existsAsFile());
    EXPECT_TRUE(pad->sampleLayers[1].file.existsAsFile());
    EXPECT_TRUE(pad->sampleLayers[0].file.isAChildOf(packagedSamples));
    EXPECT_TRUE(pad->sampleLayers[1].file.isAChildOf(packagedSamples));

    projectFile.deleteFile();
    projectFile.getSiblingFile(projectFile.getFileNameWithoutExtension()
                               + ".tracktionedit").deleteFile();
    packagedSamples.deleteRecursively();
}

TEST(QaProjectSaveLoad, KeyboardBankActiveSlotRoundTrips)
{
    // Regression guard for the bank cursor silently resetting to 0 on load.
    // The slot tracks live in the Edit and come back via attachToEdit's
    // folder scan; the cursor lives in appState and must be persisted
    // separately.
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto projFile = makeQaTempFile("qa_bank_roundtrip", "mpi");
    projFile.deleteFile();

    // Manufacture the same "Instruments" folder + slot-track layout the bank
    // produces at runtime, scoped so the local Track::Ptrs are released before
    // shutdown() tears down the Edit (holding track refs across edit.reset()
    // crashes in TE's Track destructor).
    {
        auto* edit = harness.audio().getEdit();
        ASSERT_NE(edit, nullptr);

        auto folder = edit->insertNewFolderTrack(
            tracktion::engine::TrackInsertPoint{ nullptr, nullptr }, nullptr, false);
        ASSERT_NE(folder.get(), nullptr);
        folder->setName("Instruments");
        for (int i = 0; i < 3; ++i)
        {
            auto track = edit->insertNewAudioTrack(
                tracktion::engine::TrackInsertPoint{ folder.get(), nullptr }, nullptr);
            ASSERT_NE(track.get(), nullptr);
            track->state.setProperty("instrumentSlot", i, nullptr);
            track->state.setProperty("slotName", "Instr " + juce::String(i + 1), nullptr);
            track->setName("Instr " + juce::String(i + 1));
        }

        auto& bank = harness.audio().getKeyboardBank();
        bank.attachToEdit(*edit);
        ASSERT_EQ(bank.getNumSlots(), 3);
        bank.setActiveSlot(2);
        EXPECT_EQ(bank.getActiveSlot(), 2);

        ASSERT_TRUE(harness.audio().saveProjectToFile(projFile));
    }

    harness.audio().shutdown();
    ASSERT_TRUE(harness.audio().loadProjectFromFile(projFile));

    auto& reloaded = harness.audio().getKeyboardBank();
    EXPECT_EQ(reloaded.getNumSlots(), 3);
    EXPECT_EQ(reloaded.getActiveSlot(), 2);

    projFile.deleteFile();
}

TEST(QaProjectSaveLoad, BuiltInKeyboardInstrumentAndNotesRoundTrip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto projectFile = makeQaTempFile("qa_builtin_instrument_roundtrip", "mpi");
    projectFile.deleteFile();

    const auto description = tracktion::engine::PluginManager::
        createBuiltInPluginDescription<tracktion::engine::FourOscPlugin>(true);
    auto& bank = harness.audio().getKeyboardBank();
    ASSERT_TRUE(bank.loadInstrument(description));
    auto* clip = bank.getMidiClip();
    ASSERT_NE(clip, nullptr);
    clip->getSequence().addNote(
        64, tracktion::BeatPosition::fromBeats(0.25),
        tracktion::BeatDuration::fromBeats(0.5), 91, 0, nullptr);
    ASSERT_TRUE(harness.audio().saveProjectToFile(projectFile));

    harness.audio().shutdown();
    ASSERT_TRUE(harness.audio().loadProjectFromFile(projectFile));

    auto& reloaded = harness.audio().getKeyboardBank();
    ASSERT_EQ(reloaded.getNumSlots(), 1);
    EXPECT_EQ(reloaded.getActiveSlot(), 0);
    const auto* slot = reloaded.getSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_NE(slot->plugin, nullptr);
    EXPECT_EQ(slot->displayName, description.name);
    auto* reloadedClip = reloaded.getMidiClip();
    ASSERT_NE(reloadedClip, nullptr);
    ASSERT_EQ(reloadedClip->getSequence().getNumNotes(), 1);
    const auto* note = reloadedClip->getSequence().getNote(0);
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->getNoteNumber(), 64);
    EXPECT_EQ(note->getVelocity(), 91);

    projectFile.deleteFile();
}

TEST(QaProjectSaveLoad, KeyboardPatternsRoundTripAcrossFreshEngine)
{
    auto projectFile = makeQaTempFile("qa_keyboard_patterns", "mpi");
    projectFile.deleteFile();

    {
        testharness::EngineHarness harness;
        harness.createEmptyEdit();

        const auto description = tracktion::engine::PluginManager::
            createBuiltInPluginDescription<tracktion::engine::FourOscPlugin>(true);
        auto& bank = harness.audio().getKeyboardBank();
        ASSERT_TRUE(bank.loadInstrument(description));
        auto* live = bank.getMidiClip();
        ASSERT_NE(live, nullptr);

        live->getSequence().addNote(
            60, tracktion::BeatPosition::fromBeats(0.0),
            tracktion::BeatDuration::fromBeats(0.25), 100, 0, nullptr);

        auto& sampler = harness.audio().getSampler();
        ASSERT_EQ(sampler.createPattern("Pattern 2"), 1);
        sampler.setActivePatternIndex(1);
        ASSERT_EQ(bank.getActivePatternIndex(), 1);
        live->getSequence().addNote(
            72, tracktion::BeatPosition::fromBeats(0.5),
            tracktion::BeatDuration::fromBeats(0.25), 110, 0, nullptr);

        ASSERT_TRUE(harness.audio().saveProjectToFile(projectFile));
    }

    // A distinct harness simulates an actual application restart. Reusing
    // one AudioEngine used to retain the old bank cursor and hide this bug.
    testharness::EngineHarness restored;
    ASSERT_TRUE(restored.audio().loadProjectFromFile(projectFile));

    auto& restoredBank = restored.audio().getKeyboardBank();
    EXPECT_EQ(restored.audio().getSampler().getActivePatternIndex(), 1);
    EXPECT_EQ(restoredBank.getActivePatternIndex(), 1);
    auto* live = restoredBank.getMidiClip();
    ASSERT_NE(live, nullptr);
    ASSERT_EQ(live->getSequence().getNumNotes(), 1);
    EXPECT_EQ(live->getSequence().getNote(0)->getNoteNumber(), 72);

    // The active pattern must also have been archived before the Edit file
    // was written, not merely left in the live timeline clip.
    const auto* slot = restoredBank.getSlot(0);
    ASSERT_NE(slot, nullptr);
    int archivedPatternOneNotes = -1;
    slot->patternBank.readPattern(1, [&](const tracktion::engine::MidiList& list)
    {
        archivedPatternOneNotes = list.getNumNotes();
    });
    EXPECT_EQ(archivedPatternOneNotes, 1);

    restored.audio().getSampler().setActivePatternIndex(0);
    ASSERT_EQ(live->getSequence().getNumNotes(), 1);
    EXPECT_EQ(live->getSequence().getNote(0)->getNoteNumber(), 60);
    restored.audio().getSampler().setActivePatternIndex(1);
    ASSERT_EQ(live->getSequence().getNumNotes(), 1);
    EXPECT_EQ(live->getSequence().getNote(0)->getNoteNumber(), 72);

    projectFile.deleteFile();
    projectFile.getSiblingFile(projectFile.getFileNameWithoutExtension()
                               + ".tracktionedit").deleteFile();
}

TEST(QaProjectSaveLoad, ArpeggiatorSettingsRoundTrip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& arp = harness.audio().getKeyboardArp();

    Arpeggiator::Settings s;
    s.mode      = Arpeggiator::Mode::UpDown;
    s.rateBeats = 0.5;       // 1/16
    s.octaves   = 3;
    s.gate      = 0.75f;
    s.latch     = true;
    s.channel   = 7;
    arp.setSettings(s);

    auto projFile = makeQaTempFile("qa_arp_roundtrip", "mpi");
    projFile.deleteFile();
    ASSERT_TRUE(harness.audio().saveProjectToFile(projFile));

    harness.audio().shutdown();
    ASSERT_TRUE(harness.audio().loadProjectFromFile(projFile));

    const auto r = harness.audio().getKeyboardArp().getSettings();
    EXPECT_EQ(r.mode, Arpeggiator::Mode::UpDown);
    EXPECT_DOUBLE_EQ(r.rateBeats, 0.5);
    EXPECT_EQ(r.octaves, 3);
    EXPECT_FLOAT_EQ(r.gate, 0.75f);
    EXPECT_TRUE(r.latch);
    EXPECT_EQ(r.channel, 7);

    projFile.deleteFile();
}

TEST(QaProjectSaveLoad, LoadInvalidFileWithoutCrash)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto badFile = makeQaTempFile("qa_bad_file", "edit");
    badFile.deleteFile();
    badFile.replaceWithText("this is not a valid edit file");

    EXPECT_FALSE(harness.audio().loadEditFromFile(badFile));

    badFile.deleteFile();
}

TEST(QaProjectSaveLoad, LoadNonexistentFileWithoutCrash)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto missing = makeQaTempFile("this_file_does_not_exist_qa", "edit");
    bool loaded = harness.audio().loadEditFromFile(missing);
    EXPECT_FALSE(loaded);
}

// ============================================================================
// Feature 3: Transport Control
// ============================================================================

TEST(QaTransport, PlayStopCycle)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    EXPECT_FALSE(harness.audio().isPlaying());

    harness.audio().play();
    EXPECT_TRUE(harness.audio().isPlaying());

    harness.audio().stop();
    EXPECT_FALSE(harness.audio().isPlaying());
}

TEST(QaTransport, LoopEnableDisable)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    harness.audio().setLoopSeconds(0.0, 4.0, true);
    auto snap = harness.audio().getTransportSnapshot();
    // Loop range should be set
    EXPECT_NEAR(snap.loopStartSeconds, 0.0, 0.01);
    EXPECT_TRUE(snap.isLooping);

    harness.audio().toggleLoop();
    EXPECT_FALSE(harness.audio().isLooping());
}

TEST(QaTransport, PlayModePatternAndTrack)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    harness.audio().setPlayMode(AudioEngine::PlayMode::Pattern);
    EXPECT_EQ(harness.audio().getPlayMode(), AudioEngine::PlayMode::Pattern);

    harness.audio().setPlayMode(AudioEngine::PlayMode::Track);
    EXPECT_EQ(harness.audio().getPlayMode(), AudioEngine::PlayMode::Track);
}

// ============================================================================
// Feature 4: Undo/Redo System
// ============================================================================

TEST(QaUndoRedo, RedoRestoresChangedValue)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto& engine = harness.audio();

    const int padId = pads.getPadsSnapshot().front().id;
    const int original = pads.getChokeGroup(padId);
    const int changed = (original + 2) % 7;

    pads.setChokeGroup(padId, changed);
    EXPECT_EQ(pads.getChokeGroup(padId), changed);

    EXPECT_TRUE(engine.undo());
    EXPECT_EQ(pads.getChokeGroup(padId), original);

    EXPECT_TRUE(engine.redo());
    EXPECT_EQ(pads.getChokeGroup(padId), changed);
}

// ============================================================================
// Feature 5: Sample Loading
// ============================================================================

TEST(QaSampleLoading, LoadWavToSinglePad)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("load_single", 44100);
    ASSERT_TRUE(sample.existsAsFile());

    bool loaded = harness.pads().loadSample(0, sample);
    EXPECT_TRUE(loaded);

    auto snap = harness.pads().getPadsSnapshot();
    EXPECT_TRUE(snap[0].hasSample);
}

TEST(QaSampleLoading, LoadSamplesToAll16Pads)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    for (int i = 0; i < 16; ++i)
    {
        auto sample = harness.createTemporarySampleFile("pad_" + juce::String(i), 22050);
        ASSERT_TRUE(sample.existsAsFile());
        bool loaded = harness.pads().loadSample(i, sample);
        EXPECT_TRUE(loaded) << "Failed to load sample to pad " << i;
    }

    auto snap = harness.pads().getPadsSnapshot();
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(snap[i].hasSample) << "Pad " << i << " missing sample";
}

TEST(QaSampleLoading, LoadInvalidFileWithoutCrash)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto badFile = juce::File("/tmp/nonexistent_sample_qa.wav");
    bool loaded = harness.pads().loadSample(0, badFile);
    EXPECT_FALSE(loaded);
}

// ============================================================================
// Feature 6: Pad Triggering
// ============================================================================

// ============================================================================
// Feature 7: Pattern Sequencing
// ============================================================================

TEST(QaPatternSequencing, ToggleStepsOnOff)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("step_test", 44100);
    harness.pads().loadSample(0, sample);

    // Pattern 0 should exist by default from createEmptyEdit
    auto result = harness.pads().toggleStep(0, 0, 0);
    ASSERT_TRUE(result.has_value());
    bool firstToggle = result.value();

    // Toggle again should flip
    result = harness.pads().toggleStep(0, 0, 0);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value(), firstToggle);
}

TEST(QaPatternSequencing, StepCountIsPositive)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    int steps = harness.pads().getStepCount();
    EXPECT_GT(steps, 0);
}

TEST(QaPatternSequencing, CreateMultiplePatterns)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    int p1 = harness.pads().createPattern("Pattern 2");
    EXPECT_GE(p1, 0);

    // Should be able to toggle steps in the new pattern
    auto sample = harness.createTemporarySampleFile("multi_pat", 44100);
    harness.pads().loadSample(0, sample);

    auto result = harness.pads().toggleStep(p1, 0, 0);
    ASSERT_TRUE(result.has_value());
}

// ============================================================================
// Feature 8: Truncate Sample
// ============================================================================

TEST(QaTruncateSample, SetSampleRange)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("trunc_test", 44100);
    harness.pads().loadSample(0, sample);

    bool rangeSet = harness.pads().setPadSampleRange(0, 0.1, 0.5);
    EXPECT_TRUE(rangeSet);

    auto snap = harness.pads().getPadsSnapshot();
    EXPECT_NEAR(snap[0].windowStartSeconds, 0.1, 0.01);
    EXPECT_NEAR(snap[0].windowEndSeconds, 0.5, 0.01);
}

TEST(QaTruncateSample, TruncateUpdatesSamplerExcerptNonDestructively)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("trunc_op", 44100);
    harness.pads().loadSample(0, sample);

    // Truncate from sample 1000 to 22050
    bool truncated = harness.pads().truncatePadSample(0, 1000, 22050);
    EXPECT_TRUE(truncated);

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    EXPECT_EQ(pad->sampleFile, sample);
    ASSERT_NE(pad->sampler, nullptr);
    ASSERT_GT(pad->sampler->getNumSounds(), 0);
    EXPECT_NEAR(pad->sampler->getSoundStartTime(0), 1000.0 / 44100.0, 0.001);
    EXPECT_NEAR(pad->sampler->getSoundLength(0), (22050.0 - 1000.0) / 44100.0, 0.001);
}

// ============================================================================
// Feature 9: Normalize Sample
// ============================================================================

TEST(QaNormalizeSample, NormalizeToTargetDb)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("norm_test", 44100);
    harness.pads().loadSample(0, sample);

    bool normalized = harness.pads().normalizePadSample(0, -3.0);
    EXPECT_TRUE(normalized);

    const auto* pad = harness.pads().getPad(0);
    ASSERT_NE(pad, nullptr);
    EXPECT_EQ(pad->sampleFile, sample);
    ASSERT_NE(pad->sampler, nullptr);
    ASSERT_GT(pad->sampler->getNumSounds(), 0);
    EXPECT_NEAR(pad->sampler->getSoundGainDb(0), -3.0f, 0.3f);
}

// ============================================================================
// Feature 10: Gain/Mixer Control
// ============================================================================

TEST(QaGainMixer, DefaultGainIsZero)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    float gain = harness.pads().getGainDb(0);
    EXPECT_NEAR(gain, 0.0f, 0.1f);
}

TEST(QaGainMixer, SetPadGain)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    harness.pads().setGainDb(0, -12.0f);
    float gain = harness.pads().getGainDb(0);
    EXPECT_NEAR(gain, -12.0f, 0.5f);

    harness.pads().setGainDb(0, 6.0f);
    gain = harness.pads().getGainDb(0);
    EXPECT_NEAR(gain, 6.0f, 0.5f);
}

// ============================================================================
// Feature 11: Group Save/Recall
// ============================================================================

TEST(QaGroupSaveRecall, SaveAndRecallGroup)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto sample = harness.createTemporarySampleFile("grp_test", 44100);
    harness.pads().loadSample(0, sample);

    auto& groups = harness.audio().getGroupManager();

    // Save to group 0
    bool saved = groups.saveCurrentState(0);
    EXPECT_TRUE(saved);
    EXPECT_TRUE(groups.hasGroupData(0));
    EXPECT_TRUE(groups.isGroupActive(0));
}

TEST(QaGroupSaveRecall, EightGroupsOperateIndependently)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& groups = harness.audio().getGroupManager();

    for (int i = 0; i < 8; ++i)
    {
        groups.createGroup(i);
        groups.saveCurrentState(i);
        EXPECT_TRUE(groups.hasGroupData(i)) << "Group " << i << " should have data";
    }
}

TEST(QaGroupSaveRecall, GroupColorsAssigned)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& groups = harness.audio().getGroupManager();
    groups.createGroup(0);

    const uint8_t color = groups.getGroupColor(0);
    EXPECT_GE(GroupManager::colorIndex(color), 0);
}

// ============================================================================
// Feature 12: Group Persistence
// ============================================================================

TEST(QaGroupPersistence, ClearGroupRemovesData)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& groups = harness.audio().getGroupManager();

    groups.createGroup(0);
    groups.saveCurrentState(0);
    EXPECT_TRUE(groups.hasGroupData(0));

    groups.clearGroup(0);
    EXPECT_FALSE(groups.isGroupActive(0));
}

// ============================================================================
// Feature 13: TE-Native Step Sequencing
// Post-MidiClip-unification: patterns live as per-pad MidiClips on pad tracks.
// Old assertions against a shared StepClip rewritten to the MidiClip
// equivalent — same test intent, new backing store.
// ============================================================================

namespace
{
te::MidiClip* padMidiClip(SamplerInstrument& pads, int padIndex)
{
    auto* track = pads.getTrack(padIndex);
    if (track == nullptr)
        return nullptr;
    for (auto* c : track->getClips())
        if (auto* mc = dynamic_cast<te::MidiClip*>(c))
            return mc;
    return nullptr;
}
}

TEST(QaTeNativeSequencing, EachPadOwnsAMidiClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    for (int i = 0; i < pads.getPadCount(); ++i)
    {
        auto* mc = padMidiClip(pads, i);
        ASSERT_NE(mc, nullptr) << "pad " << i << " is missing its MidiClip";
    }
}

TEST(QaTeNativeSequencing, PadTracksHaveNoStepClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    for (int i = 0; i < pads.getPadCount(); ++i)
    {
        auto* pad = pads.getPad(i);
        ASSERT_NE(pad, nullptr);
        ASSERT_NE(pad->track, nullptr);

        for (auto* clip : pad->track->getClips())
            EXPECT_EQ(dynamic_cast<te::StepClip*>(clip), nullptr)
                << "Pad track " << i << " must not host a StepClip after unification";
    }
}

TEST(QaTeNativeSequencing, ToggleStepUpdatesMidiClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto sample = harness.createTemporarySampleFile("toggle_test", 44100);
    pads.loadSample(0, sample);

    auto result = pads.toggleStep(0, 0, 3);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value());

    auto snaps = pads.getPadsSnapshot();
    ASSERT_FALSE(snaps.empty());
    ASSERT_FALSE(snaps[0].patterns.empty());
    EXPECT_TRUE(snaps[0].patterns[0].steps[3]);

    result = pads.toggleStep(0, 0, 3);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());

    snaps = pads.getPadsSnapshot();
    EXPECT_FALSE(snaps[0].patterns[0].steps[3]);
}

TEST(QaTeNativeSequencing, SetStepUpdatesMidiClip)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto sample = harness.createTemporarySampleFile("set_test", 44100);
    pads.loadSample(0, sample);

    EXPECT_TRUE(pads.setStep(0, 0, 7, true));

    auto snaps = pads.getPadsSnapshot();
    ASSERT_FALSE(snaps.empty());
    ASSERT_FALSE(snaps[0].patterns.empty());
    EXPECT_TRUE(snaps[0].patterns[0].steps[7]);

    EXPECT_TRUE(pads.setStep(0, 0, 7, false));
    snaps = pads.getPadsSnapshot();
    EXPECT_FALSE(snaps[0].patterns[0].steps[7]);
}

TEST(QaTeNativeSequencing, MultiplePadsSequenceIndependently)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto sample = harness.createTemporarySampleFile("multi_pad", 44100);
    pads.loadSample(0, sample);
    pads.loadSample(1, sample);

    pads.setStep(0, 0, 0, true);
    pads.setStep(0, 1, 4, true);

    auto snaps = pads.getPadsSnapshot();
    ASSERT_GE(snaps.size(), size_t(2));
    ASSERT_FALSE(snaps[0].patterns.empty());
    ASSERT_FALSE(snaps[1].patterns.empty());

    EXPECT_TRUE(snaps[0].patterns[0].steps[0]);
    EXPECT_FALSE(snaps[0].patterns[0].steps[4]);
    EXPECT_FALSE(snaps[1].patterns[0].steps[0]);
    EXPECT_TRUE(snaps[1].patterns[0].steps[4]);
}

TEST(QaTeNativeSequencing, CreatePatternExpandsNumPatterns)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    const int before = pads.getNumPatterns();
    int newIdx = pads.createPattern("Test Pattern");
    ASSERT_GE(newIdx, 0);
    EXPECT_EQ(pads.getNumPatterns(), before + 1);
    EXPECT_EQ(newIdx, before);
}

TEST(QaTeNativeSequencing, StepInNewPatternRoundTrips)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    auto sample = harness.createTemporarySampleFile("newpat", 44100);
    pads.loadSample(0, sample);

    int patIdx = pads.createPattern("P2");
    ASSERT_GE(patIdx, 0);
    EXPECT_TRUE(pads.setStep(patIdx, 0, 10, true));

    auto snaps = pads.getPadsSnapshot();
    ASSERT_FALSE(snaps.empty());
    ASSERT_GE(static_cast<int>(snaps[0].patterns.size()), patIdx + 1);
    EXPECT_TRUE(snaps[0].patterns[static_cast<size_t>(patIdx)].steps[10]);
}

TEST(QaTeNativeSequencing, PadTracksAreAudible)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto& pads = harness.pads();
    for (int i = 0; i < pads.getPadCount(); ++i)
    {
        auto* track = pads.getTrack(i);
        ASSERT_NE(track, nullptr);
        EXPECT_FALSE(track->isMuted(false)) << "Pad track " << i << " should remain audible";
    }
}

TEST(QaTeNativeSequencing, PadPatternRangeIsStable)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    auto range = harness.pads().getPatternEditTimeRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_GE(range->getStart().inSeconds(), 0.0);
    EXPECT_GT(range->getEnd().inSeconds(), range->getStart().inSeconds());
}
