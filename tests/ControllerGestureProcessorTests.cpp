#include <gtest/gtest.h>

#include "../src/control/ControllerGestureProcessor.h"
#include "../src/control/ControllerHost.h"
#include "../src/control/HardwareConstants.h"
#include "../src/control/IController.h"

#include <unordered_map>

namespace
{
class RecordingController final : public IController
{
public:
    bool isConnected() const noexcept override { return true; }
    void shutdown() override {}
    bool sendDisplayFrame(int, const juce::Image&) override { return true; }
    bool clearDisplay(int, uint16_t) override { return true; }
    void clearAllDisplays() override {}

    bool setMonoLed(const std::string& name, uint8_t brightness) override
    {
        ++monoWrites[name];
        if (failNextMonoWrite)
        {
            failNextMonoWrite = false;
            return false;
        }
        monoLeds[name] = brightness;
        return true;
    }

    bool setIndexedLed(const std::string& name, uint8_t colorIndex) override
    {
        indexedLeds[name] = colorIndex;
        return true;
    }

    void setButtonActive(const std::string&, bool) override {}
    void setButtonBrightness(const std::string&, uint8_t) override {}
    void resetAllLeds() override {}

    std::unordered_map<std::string, uint8_t> monoLeds;
    std::unordered_map<std::string, uint8_t> indexedLeds;
    std::unordered_map<std::string, int> monoWrites;
    bool failNextMonoWrite { false };
};

TEST(ControllerGestureProcessorTest, AppliesTheSameMomentaryLedFeedbackForEveryBackend)
{
    ControllerHost host(nullptr, false);
    RecordingController output;
    ControllerGestureProcessor processor(host, output);

    processor.setButtonActive("browserPlugin", true);
    EXPECT_EQ(output.monoLeds["browserPlugin"], HardwareConstants::kLedDim);

    processor.handleButton("browserPlugin", true);
    EXPECT_EQ(output.monoLeds["browserPlugin"], HardwareConstants::kLedBright);

    processor.handleButton("browserPlugin", false);
    EXPECT_EQ(output.monoLeds["browserPlugin"], HardwareConstants::kLedDim);
}

TEST(ControllerGestureProcessorTest, TransportLedIgnoresRawMomentaryFeedback)
{
    ControllerHost host(nullptr, false);
    RecordingController output;
    ControllerGestureProcessor processor(host, output);

    processor.setButtonBrightness("recCountIn", HardwareConstants::kLedBright);
    ASSERT_EQ(output.monoLeds["recCountIn"], HardwareConstants::kLedBright);

    processor.handleButton("recCountIn", true);
    processor.handleButton("recCountIn", false);

    EXPECT_EQ(output.monoLeds["recCountIn"], HardwareConstants::kLedBright);
    EXPECT_EQ(output.monoWrites["recCountIn"], 1);
}

TEST(ControllerGestureProcessorTest, MapsSamplingBrightnessToItsIndexedLed)
{
    ControllerHost host(nullptr, false);
    RecordingController output;
    ControllerGestureProcessor processor(host, output);

    processor.setButtonBrightness("sampling", HardwareConstants::kLedDim);
    EXPECT_EQ(output.indexedLeds["sampling"], HardwareConstants::kColorWhite);

    processor.handleButton("sampling", true);
    EXPECT_EQ(output.indexedLeds["sampling"], HardwareConstants::kColorWhite);

    processor.handleButton("sampling", false);
    EXPECT_EQ(output.indexedLeds["sampling"], HardwareConstants::kColorWhite);
}

TEST(ControllerGestureProcessorTest, CachesSuccessfulMonoLedWritesAndRetriesFailures)
{
    ControllerHost host(nullptr, false);
    RecordingController output;
    ControllerGestureProcessor processor(host, output);

    processor.setButtonBrightness("play", HardwareConstants::kLedDim);
    processor.setButtonBrightness("play", HardwareConstants::kLedDim);
    EXPECT_EQ(output.monoWrites["play"], 1);

    output.failNextMonoWrite = true;
    processor.setButtonBrightness("play", HardwareConstants::kLedBright);
    processor.setButtonBrightness("play", HardwareConstants::kLedBright);
    EXPECT_EQ(output.monoWrites["play"], 3);
    EXPECT_EQ(output.monoLeds["play"], HardwareConstants::kLedBright);

    processor.invalidateLedOutputCache();
    processor.setButtonBrightness("play", HardwareConstants::kLedBright);
    EXPECT_EQ(output.monoWrites["play"], 4);
}

TEST(ControllerGestureProcessorTest, RewritesAvailableLedAfterHardwareResetInvalidatesCache)
{
    ControllerHost host(nullptr, false);
    RecordingController output;
    ControllerGestureProcessor processor(host, output);

    processor.setButtonActive("browserPlugin", true);
    ASSERT_EQ(output.monoWrites["browserPlugin"], 1);
    ASSERT_EQ(output.monoLeds["browserPlugin"], HardwareConstants::kLedDim);

    output.monoLeds["browserPlugin"] = HardwareConstants::kLedOff;
    processor.invalidateLedOutputCache();
    processor.setButtonActive("browserPlugin", true);

    EXPECT_EQ(output.monoWrites["browserPlugin"], 2);
    EXPECT_EQ(output.monoLeds["browserPlugin"], HardwareConstants::kLedDim);
}

TEST(ControllerGestureProcessorTest, DoesNotTreatNonMonoInputsAsMonoLeds)
{
    ControllerHost host(nullptr, false);
    RecordingController output;
    ControllerGestureProcessor processor(host, output);

    processor.handleButton("g1", true);
    processor.handleButton("navPush", true);
    processor.handleButton("knobTouch1", true);

    EXPECT_FALSE(output.monoLeds.contains("g1"));
    EXPECT_FALSE(output.monoLeds.contains("navPush"));
    EXPECT_FALSE(output.monoLeds.contains("knobTouch1"));
}
} // namespace
