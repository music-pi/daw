#pragma once

#include <cstdint>
#include <functional>
#include <optional>

/**
 * Generic encoder handler for consistent knob/encoder input handling.
 * 
 * Based on best practices from encoder handling libraries, this handler:
 * - Tracks touch state to enable preview mode
 * - Only persists values when encoder is released
 * - Provides smooth delta-based adjustments
 * - Supports fine/coarse adjustment modes
 */
class EncoderHandler
{
public:
    /**
     * Configuration for an encoder parameter
     */
    struct Config
    {
        double value { 0.0 };
        double minimum { 0.0 };
        double maximum { 1.0 };
        double step { 0.01 };
        double fineStep { 0.001 };
    };

    /**
     * Callback when value changes (during preview or commit)
     * @param value The new value
     * @param isPreview True if this is a preview (not yet committed)
     */
    using ValueChangedCallback = std::function<void(double value, bool isPreview)>;

    /**
     * Callback when value should be persisted (encoder released)
     * @param value The final value to persist
     */
    using ValueCommitCallback = std::function<void(double value)>;

    EncoderHandler();
    ~EncoderHandler() = default;

    /**
     * Set the encoder configuration
     */
    void setConfig(const Config& config);

    /**
     * Get current configuration
     */
    const Config& getConfig() const { return config; }

    /**
     * Set callbacks
     */
    void setValueChangedCallback(ValueChangedCallback callback);
    void setValueCommitCallback(ValueCommitCallback callback);

    /**
     * Handle encoder delta (rotation)
     * @param delta Encoder delta (positive = clockwise, negative = counter-clockwise)
     * @param shift Fine adjustment mode if true
     */
    void handleDelta(int delta, bool shift = false);

    /**
     * Handle encoder absolute value (0-999 range, like MK3 knobs)
     * Maps the absolute value to the parameter range for smooth, accurate updates
     * @param absoluteValue Absolute value from hardware (0-999)
     * @param maxAbsolute Maximum absolute value (typically 999 for MK3)
     */
    void handleAbsolute(uint16_t absoluteValue, uint16_t maxAbsolute = 999);

    /**
     * Handle encoder touch state
     * @param touched True when encoder is touched, false when released
     */
    void handleTouch(bool touched);

    /**
     * Get current value (preview if touching, committed otherwise)
     */
    double getValue() const;

    /**
     * Get preview value (value while touching, even if not committed)
     */
    double getPreviewValue() const;

    /**
     * Check if encoder is currently being touched
     */
    bool isTouched() const { return isTouching; }

    /**
     * Force commit current preview value (useful for cleanup)
     */
    void commit();

    /**
     * Reset to a specific value (clears preview state)
     */
    void setValue(double value, bool notify = true);

private:
    Config config;
    double previewValue { 0.0 };
    bool isTouching { false };
    bool hasUncommittedChanges { false };

    ValueChangedCallback valueChangedCallback;
    ValueCommitCallback valueCommitCallback;

    void updateValue(double newValue, bool isPreview);
    double clampValue(double value) const;
};

