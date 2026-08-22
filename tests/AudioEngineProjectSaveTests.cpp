#include <gtest/gtest.h>

#include "harness/EngineHarness.h"
#include "../src/engine/GroupManager.h"
#include "../src/engine/SamplerInstrument.h"

namespace
{

class TemporaryProjectDirectory
{
public:
    TemporaryProjectDirectory()
        : directory(juce::File::createTempFile(""))
    {
        directory.deleteFile();
        directory.createDirectory();
    }

    ~TemporaryProjectDirectory()
    {
        directory.deleteRecursively();
    }

    juce::File directory;
};

} // namespace

TEST(AudioEngineProjectSaveTests, SuccessfulResaveKeepsPreviousGenerationAsBackup)
{
    TemporaryProjectDirectory temp;
    const auto project = temp.directory.getChildFile("project.mpi");
    const auto edit = temp.directory.getChildFile("project.tracktionedit");

    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    ASSERT_TRUE(harness.audio().saveProjectToFile(project));
    const auto firstProject = project.loadFileAsString();
    const auto firstEdit = edit.loadFileAsString();
    ASSERT_TRUE(firstProject.isNotEmpty());
    ASSERT_TRUE(firstEdit.isNotEmpty());

    auto* loadedEdit = harness.audio().getEdit();
    ASSERT_NE(loadedEdit, nullptr);
    loadedEdit->tempoSequence.getTempoAt(
        tracktion::TimePosition::fromSeconds(0.0)).setBpm(127.0);
    ASSERT_TRUE(harness.audio().saveProjectToFile(project));

    const auto projectBackup = temp.directory.getChildFile("project.mpi.bak");
    const auto editBackup = temp.directory.getChildFile("project.tracktionedit.bak");
    ASSERT_TRUE(projectBackup.existsAsFile());
    ASSERT_TRUE(editBackup.existsAsFile());
    EXPECT_EQ(projectBackup.loadFileAsString(), firstProject);
    EXPECT_EQ(editBackup.loadFileAsString(), firstEdit);
}

TEST(AudioEngineProjectSaveTests, WrapperWriteFailureRestoresPreviousEditGeneration)
{
    TemporaryProjectDirectory temp;
    const auto project = temp.directory.getChildFile("project.mpi");
    const auto edit = temp.directory.getChildFile("project.tracktionedit");

    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    ASSERT_TRUE(harness.audio().saveProjectToFile(project));
    const auto firstEdit = edit.loadFileAsString();
    ASSERT_TRUE(firstEdit.isNotEmpty());

    auto* loadedEdit = harness.audio().getEdit();
    ASSERT_NE(loadedEdit, nullptr);
    loadedEdit->tempoSequence.getTempoAt(
        tracktion::TimePosition::fromSeconds(0.0)).setBpm(131.0);

    ASSERT_TRUE(project.deleteFile());
    ASSERT_TRUE(project.createDirectory());
    ASSERT_TRUE(project.getChildFile("obstruction").replaceWithText("keep"));
    EXPECT_FALSE(harness.audio().saveProjectToFile(project));
    EXPECT_EQ(edit.loadFileAsString(), firstEdit);
}

TEST(AudioEngineProjectSaveTests, IndependentGroupBanksRoundTripWithPatterns)
{
    TemporaryProjectDirectory temp;
    const auto project = temp.directory.getChildFile("groups.mpi");

    testharness::EngineHarness source;
    source.createEmptyEdit();
    auto sampleA = source.createTemporarySampleFile("persist_group_a", 4410);
    auto sampleB = source.createTemporarySampleFile("persist_group_b", 4410);

    auto& groups = source.audio().getGroupManager();
    auto* groupA = source.audio().getSamplerForGroup(0);
    ASSERT_NE(groupA, nullptr);
    ASSERT_TRUE(groupA->loadSample(0, sampleA));
    ASSERT_TRUE(groupA->setStep(0, 0, 0, true));

    groups.createGroup(1);
    auto* groupB = source.audio().getSamplerForGroup(1);
    ASSERT_NE(groupB, nullptr);
    ASSERT_TRUE(groupB->loadSample(0, sampleB));
    groupB->setGainDb(0, -9.0f);
    ASSERT_TRUE(groupB->setStep(0, 0, 8, true));
    ASSERT_TRUE(source.audio().saveProjectToFile(project));

    testharness::EngineHarness restored;
    ASSERT_TRUE(restored.audio().loadProjectFromFile(project));
    EXPECT_EQ(restored.audio().getGroupManager().getActiveGroupIndex(), 1);

    const auto* restoredA = restored.audio().getSamplerForGroup(0);
    const auto* restoredB = restored.audio().getSamplerForGroup(1);
    ASSERT_NE(restoredA, nullptr);
    ASSERT_NE(restoredB, nullptr);
    ASSERT_TRUE(restoredA->getPad(0)->sampleFile.existsAsFile());
    ASSERT_TRUE(restoredB->getPad(0)->sampleFile.existsAsFile());
    EXPECT_NE(restoredA->getPad(0)->sampleFile,
              restoredB->getPad(0)->sampleFile);
    EXPECT_NEAR(restoredB->getGainDb(0), -9.0f, 0.01f);

    const auto patternsA = restoredA->getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    const auto patternsB = restoredB->getPadsSnapshot(
        SamplerInstrument::SnapshotContent::Patterns);
    ASSERT_FALSE(patternsA.empty());
    ASSERT_FALSE(patternsB.empty());
    EXPECT_TRUE(patternsA[0].patterns[0].steps[0]);
    EXPECT_TRUE(patternsB[0].patterns[0].steps[8]);
}
