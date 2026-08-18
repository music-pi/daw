#include <gtest/gtest.h>

#include "harness/EngineHarness.h"

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
