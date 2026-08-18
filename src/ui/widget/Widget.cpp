#include "Widget.h"

#include "../hw/HardwareState.h"
#include "../../engine/AudioEngine.h"
#include "../UiRefresh.h"
#include "WindowManager.h"

HardwareState& Widget::hw()
{
    jassert(hardware_ != nullptr);
    return *hardware_;
}

const HardwareState& Widget::hw() const
{
    jassert(hardware_ != nullptr);
    return *hardware_;
}

AudioEngine& Widget::engine()
{
    jassert(audioEngine_ != nullptr);
    return *audioEngine_;
}

const AudioEngine& Widget::engine() const
{
    jassert(audioEngine_ != nullptr);
    return *audioEngine_;
}

void Widget::showToast(ToastKind kind, const juce::String& msg, int durationMs)
{
    if (windowManager_ == nullptr) return;
    const auto side = (panelOffset_ == 0) ? DisplaySide::Left : DisplaySide::Right;
    windowManager_->showToast(side, kind, msg, durationMs);
}

ToastHandle Widget::showLoadingToast(const juce::String& msg)
{
    if (windowManager_ == nullptr) return {};
    const auto side = (panelOffset_ == 0) ? DisplaySide::Left : DisplaySide::Right;
    return windowManager_->showLoadingToast(side, msg);
}

void Widget::repaint()
{
    JUCE_ASSERT_MESSAGE_THREAD;
    juce::Component::repaint();
    requestUiRefresh(*this);
}

void Widget::repaint(int x, int y, int width, int height)
{
    JUCE_ASSERT_MESSAGE_THREAD;
    juce::Component::repaint(x, y, width, height);
    requestUiRefresh(*this);
}

void Widget::repaint(juce::Rectangle<int> area)
{
    JUCE_ASSERT_MESSAGE_THREAD;
    juce::Component::repaint(area);
    requestUiRefresh(*this);
}
