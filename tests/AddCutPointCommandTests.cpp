#include <gtest/gtest.h>

#include "../src/engine/commands/AddCutPointCommand.h"

#include <vector>

class AddCutPointCommandTest : public ::testing::Test
{
protected:
    std::vector<double> cutPoints;
    int changeCount = 0;
    static constexpr double kSampleRate = 44100.0;
    static constexpr double kTotalSeconds = 2.0;

    std::function<void(const std::vector<double>&)> onChange =
        [this](const std::vector<double>& updated) { cutPoints = updated; ++changeCount; };
};

TEST_F(AddCutPointCommandTest, PerformAddsCutPoint)
{
    AddCutPointCommand cmd(cutPoints, 1.0, kSampleRate, kTotalSeconds, onChange);
    EXPECT_TRUE(cmd.perform());
    EXPECT_EQ(cutPoints.size(), 1u);
    EXPECT_NEAR(cutPoints[0], 1.0, 1.0 / kSampleRate);
    EXPECT_EQ(changeCount, 1);
}

TEST_F(AddCutPointCommandTest, UndoRemovesCutPoint)
{
    AddCutPointCommand cmd(cutPoints, 1.0, kSampleRate, kTotalSeconds, onChange);
    EXPECT_TRUE(cmd.perform());
    EXPECT_TRUE(cmd.undo());
    EXPECT_TRUE(cutPoints.empty());
    EXPECT_EQ(changeCount, 2);
}

TEST_F(AddCutPointCommandTest, RedoAfterUndoRestoresCutPoint)
{
    AddCutPointCommand cmd(cutPoints, 1.0, kSampleRate, kTotalSeconds, onChange);
    cmd.perform();
    cmd.undo();

    // Redo: perform again after undo - reset performed flag
    // AddCutPointCommand tracks performed state, second perform returns true (already performed guard)
    // So we create a new command for redo
    AddCutPointCommand redoCmd(cutPoints, 1.0, kSampleRate, kTotalSeconds, onChange);
    EXPECT_TRUE(redoCmd.perform());
    EXPECT_EQ(cutPoints.size(), 1u);
}

TEST_F(AddCutPointCommandTest, CutPointsRemainSorted)
{
    AddCutPointCommand cmd1(cutPoints, 1.5, kSampleRate, kTotalSeconds, onChange);
    cmd1.perform();

    // Second command sees updated state (mimics real undo-manager usage)
    AddCutPointCommand cmd2(cutPoints, 0.5, kSampleRate, kTotalSeconds, onChange);
    cmd2.perform();

    ASSERT_EQ(cutPoints.size(), 2u);
    EXPECT_LT(cutPoints[0], cutPoints[1]);
}

TEST_F(AddCutPointCommandTest, CutPointClampedToTotalSeconds)
{
    AddCutPointCommand cmd(cutPoints, 5.0, kSampleRate, kTotalSeconds, onChange);
    cmd.perform();

    ASSERT_EQ(cutPoints.size(), 1u);
    EXPECT_LE(cutPoints[0], kTotalSeconds);
}

TEST_F(AddCutPointCommandTest, DuplicatePerformIsIdempotent)
{
    AddCutPointCommand cmd(cutPoints, 1.0, kSampleRate, kTotalSeconds, onChange);
    EXPECT_TRUE(cmd.perform());
    EXPECT_TRUE(cmd.perform()); // Second perform should be no-op
    EXPECT_EQ(cutPoints.size(), 1u);
    EXPECT_EQ(changeCount, 1); // onChange only called once
}

TEST_F(AddCutPointCommandTest, UndoWithoutPerformReturnsFalse)
{
    AddCutPointCommand cmd(cutPoints, 1.0, kSampleRate, kTotalSeconds, onChange);
    EXPECT_FALSE(cmd.undo());
    EXPECT_EQ(changeCount, 0);
}
