#include "EncoderHandler.h"
#include <algorithm>
#include <cmath>

EncoderHandler::EncoderHandler()
    : previewValue(0.0)
    , isTouching(false)
    , hasUncommittedChanges(false)
{
}

void EncoderHandler::setConfig(const Config& newConfig)
{
    config = newConfig;
    previewValue = config.value;
    hasUncommittedChanges = false;
}

void EncoderHandler::setValueChangedCallback(ValueChangedCallback callback)
{
    valueChangedCallback = std::move(callback);
}

void EncoderHandler::setValueCommitCallback(ValueCommitCallback callback)
{
    valueCommitCallback = std::move(callback);
}

void EncoderHandler::handleDelta(int delta, bool shift)
{
    if (delta == 0)
        return;

    const double step = shift ? config.fineStep : config.step;
    const double stepDelta = static_cast<double>(delta) * step;
    double newValue = previewValue + stepDelta;

    newValue = clampValue(newValue);

    // Only update if value actually changed
    if (std::abs(newValue - previewValue) < 1e-9)
        return;

    updateValue(newValue, isTouching);
    
    if (isTouching)
    {
        hasUncommittedChanges = true;
    }
}

void EncoderHandler::handleAbsolute(uint16_t absoluteValue, uint16_t maxAbsolute)
{
    if (maxAbsolute == 0)
        return;

    // Map absolute value (0-maxAbsolute) to parameter range (minimum-maximum)
    const double normalized = static_cast<double>(absoluteValue) / static_cast<double>(maxAbsolute);
    const double span = config.maximum - config.minimum;
    double newValue = config.minimum + (normalized * span);

    newValue = std::max(config.minimum, std::min(config.maximum, newValue));

    // Only update if value actually changed (avoid unnecessary updates)
    if (std::abs(newValue - previewValue) < 1e-9)
        return;

    updateValue(newValue, isTouching);
    
    if (isTouching)
    {
        hasUncommittedChanges = true;
    }
}

void EncoderHandler::handleTouch(bool touched)
{
    if (touched == isTouching)
        return; // No state change

    if (touched)
    {
        // Touch started: initialize preview value from current config value
        isTouching = true;
        previewValue = config.value;
        hasUncommittedChanges = false;
    }
    else
    {
        // Touch released: commit preview value if changed
        isTouching = false;
        
        if (hasUncommittedChanges)
        {
            commit();
        }
        
        hasUncommittedChanges = false;
    }
}

double EncoderHandler::getValue() const
{
    return isTouching ? previewValue : config.value;
}

double EncoderHandler::getPreviewValue() const
{
    return previewValue;
}

void EncoderHandler::commit()
{
    if (!hasUncommittedChanges)
        return;

    const double finalValue = previewValue;
    config.value = finalValue;
    hasUncommittedChanges = false;

    if (valueCommitCallback)
    {
        valueCommitCallback(finalValue);
    }

    // Also notify value changed with committed state
    if (valueChangedCallback)
    {
        valueChangedCallback(finalValue, false);
    }
}

void EncoderHandler::setValue(double value, bool notify)
{
    const double clamped = clampValue(value);
    config.value = clamped;
    previewValue = clamped;
    hasUncommittedChanges = false;

    if (notify && valueChangedCallback)
    {
        valueChangedCallback(clamped, false);
    }
}

void EncoderHandler::updateValue(double newValue, bool isPreview)
{
    previewValue = newValue;

    if (valueChangedCallback)
    {
        valueChangedCallback(newValue, isPreview);
    }
}

double EncoderHandler::clampValue(double value) const
{
    return std::max(config.minimum, std::min(config.maximum, value));
}

