#pragma once

#include <vector>

#include <unordered_map>

#include "../../src/control/ControllerHost.h"

namespace testharness
{

class MockControllerHost : public ControllerHost
{
public:
    MockControllerHost();
    ~MockControllerHost() override = default;

    void setButtonBrightness(const std::string& buttonName, uint8_t brightness) override;
    void pushDisplayFrame(int screenIndex, const juce::Image& image) override;

    void triggerButton(const std::string& buttonName, bool pressed = true, bool shift = false);

    uint8_t getButtonBrightness(const std::string& buttonName) const;

    int pushDisplayFrameCount() const noexcept { return pushDisplayFrameCount_; }
    const std::vector<int>& pushedDisplayScreens() const noexcept { return pushedDisplayScreens_; }
    void resetCounters() noexcept
    {
        pushDisplayFrameCount_ = 0;
        pushedDisplayScreens_.clear();
    }

private:
    std::unordered_map<std::string, uint8_t> brightnessByButton;
    int pushDisplayFrameCount_ { 0 };
    std::vector<int> pushedDisplayScreens_;
};

} // namespace testharness
