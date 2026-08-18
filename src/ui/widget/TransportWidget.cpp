#include "TransportWidget.h"

#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../hw/HardwareState.h"

using namespace HardwareConstants;

WidgetDescriptor TransportWidget::describe() const
{
    return { "transport", 0, false, DisplayConstraint::Any };
}

void TransportWidget::onActivated(int panelOffset)
{
    panelOffset_ = panelOffset;
    updateLeds();
}

void TransportWidget::onDeactivated()
{
}

std::vector<std::string> TransportWidget::requiredResources(int /*page*/)
{
    return { "play", "stop", "recCountIn", "restartLoop" };
}

void TransportWidget::handleButton(const controller_events::ButtonEvent& e)
{
    if (!e.pressed)
        return;

    if (e.name == "play")
    {
        engine().play();
        updateLeds();
    }
    else if (e.name == "stop")
    {
        engine().stop();
        updateLeds();
    }
    else if (e.name == "restartLoop")
    {
        engine().toggleLoop();
        updateLeds();
    }
}

void TransportWidget::onUiHostTick()
{
    updateLeds();
}

void TransportWidget::updateLeds()
{
    auto& hardware = hw();
    const auto id = describe().id.toStdString();

    const bool available   = engine().hasEdit();
    const bool playing     = available && engine().isPlaying();
    const bool looping     = available && engine().isLooping();
    const bool paused      = available && engine().isPaused();
    const bool captureSuite = engine().isAudioCaptureSuiteActive();
    const bool captureRecording = engine().isAudioCaptureRecording();
    const bool recordArmed = available && engine().isRecordArmed();
    const bool countingIn  = available && engine().isCountingIn();

    const bool blinkOn = ((juce::Time::getMillisecondCounter() / 250) % 2) == 0; // ~2Hz blink

    // Play LED
    uint8_t playValue = kLedOff;
    if (available)
    {
        if (playing)
            playValue = kLedBright;
        else if (paused)
            playValue = blinkOn ? kLedBright : kLedOff;
        else
            playValue = kLedDim;
    }
    hardware.setLed("play", playValue, id);

    // Stop LED
    uint8_t stopValue = kLedOff;
    if (available)
        stopValue = playing ? kLedDim : kLedBright;
    hardware.setLed("stop", stopValue, id);

    // Record LED: 4-state indicator.
    //   dim    → an edit is loaded, record is available but not armed
    //   bright → user armed record; transport still stopped
    //   blink  → count-in pre-roll is running
    //   bright → armed + playing past count-in = actively capturing
    uint8_t recValue = kLedOff;
    if (captureSuite)
        recValue = captureRecording ? kLedBright : kLedMedium;
    else if (available)
    {
        if (!recordArmed)
            recValue = kLedDim;
        else if (countingIn)
            recValue = blinkOn ? kLedBright : kLedOff;
        else if (playing)
            recValue = kLedBright;
        else
            recValue = kLedBright;
    }
    hardware.setLed("recCountIn", recValue, id);

    // Loop LED
    uint8_t loopValue = kLedOff;
    if (available)
        loopValue = looping ? kLedBright : kLedDim;
    hardware.setLed("restartLoop", loopValue, id);
}
