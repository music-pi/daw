#include <gtest/gtest.h>

#include "../src/input/EncoderHandler.h"

namespace
{

EncoderHandler::Config makeConfig(double value = 0.5,
                                  double min = 0.0,
                                  double max = 1.0,
                                  double step = 0.1,
                                  double fine = 0.01)
{
    EncoderHandler::Config config;
    config.value = value;
    config.minimum = min;
    config.maximum = max;
    config.step = step;
    config.fineStep = fine;
    return config;
}

class EncoderHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        handler.setConfig(makeConfig());
    }

    EncoderHandler handler;
};

TEST_F(EncoderHandlerTest, DeltaAdjustmentsRespectShiftAndClamp)
{
    double lastValue = 0.0;
    bool lastPreview = false;
    handler.setValueChangedCallback([&](double value, bool preview)
    {
        lastValue = value;
        lastPreview = preview;
    });

    handler.handleTouch(true);
    handler.handleDelta(1); // step 0.1
    EXPECT_DOUBLE_EQ(lastValue, 0.6);
    EXPECT_TRUE(lastPreview);

    handler.handleDelta(5, true); // fine step increments by 0.05
    EXPECT_DOUBLE_EQ(lastValue, 0.65);

    handler.handleDelta(-100); // clamp to minimum
    EXPECT_DOUBLE_EQ(lastValue, 0.0);

    handler.handleDelta(2000); // clamp to maximum
    EXPECT_DOUBLE_EQ(lastValue, 1.0);
}

TEST_F(EncoderHandlerTest, TouchReleaseCommitsValueOnce)
{
    double committedValue = 0.0;
    int commitCount = 0;
    handler.setValueChangedCallback([](double, bool) {});
    handler.setValueCommitCallback([&](double value)
    {
        committedValue = value;
        ++commitCount;
    });

    handler.handleTouch(true);
    handler.handleDelta(3);
    handler.handleTouch(false);

    EXPECT_DOUBLE_EQ(committedValue, 0.8);
    EXPECT_EQ(commitCount, 1);
    EXPECT_DOUBLE_EQ(handler.getValue(), 0.8);
}

TEST_F(EncoderHandlerTest, CommitApiFlushesPendingPreview)
{
    double committedValue = 0.0;
    handler.setValueChangedCallback([](double, bool) {});
    handler.setValueCommitCallback([&](double value)
    {
        committedValue = value;
    });

    handler.handleTouch(true);
    handler.handleDelta(1);
    handler.commit();

    EXPECT_DOUBLE_EQ(committedValue, 0.6);
    EXPECT_DOUBLE_EQ(handler.getValue(), 0.6);
}

TEST_F(EncoderHandlerTest, AbsoluteUpdatesMapRange)
{
    double lastValue = 0.0;
    handler.setValueChangedCallback([&](double value, bool) { lastValue = value; });

    handler.handleTouch(true);
    handler.handleAbsolute(0, 999);
    EXPECT_DOUBLE_EQ(lastValue, 0.0);

    handler.handleAbsolute(999, 999);
    EXPECT_DOUBLE_EQ(lastValue, 1.0);

    handler.handleAbsolute(500, 1000);
    EXPECT_NEAR(lastValue, 0.5, 1e-3);
}

TEST_F(EncoderHandlerTest, SetValueResetsPreviewState)
{
    double notifiedValue = 0.0;
    handler.setValueChangedCallback([&](double value, bool) { notifiedValue = value; });

    handler.setValue(0.25);
    EXPECT_DOUBLE_EQ(handler.getValue(), 0.25);
    EXPECT_DOUBLE_EQ(handler.getPreviewValue(), 0.25);
    EXPECT_DOUBLE_EQ(notifiedValue, 0.25);

    handler.setValue(0.75, false);
    EXPECT_DOUBLE_EQ(handler.getValue(), 0.75);
    EXPECT_DOUBLE_EQ(handler.getPreviewValue(), 0.75);
    EXPECT_DOUBLE_EQ(notifiedValue, 0.25); // unchanged because notify=false
}

} // namespace

