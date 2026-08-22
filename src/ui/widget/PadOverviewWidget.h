#pragma once

#include <array>
#include <functional>
#include <unordered_map>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../engine/SamplerInstrument.h"
#include "../../input/InputManager.h"
#include "../components/PadGridComponent.h"
#include "Widget.h"

class ControllerHost;

/**
 * PadOverviewWidget — 4x4 pad grid displaying sample names, waveform
 * thumbnails, and flash-on-trigger animation.
 *
 * Uses the Widget base class with HardwareState LED ownership.
 *
 * Grid rendering and LED publishing are delegated to PadGridComponent.
 * PadOverviewWidget builds a Tile array each tick (refreshTiles()) and
 * supplies a waveform-thumbnail overlay callback.
 */
class PadOverviewWidget : public Widget,
                          public SamplerInstrument::Listener
{
public:
    PadOverviewWidget();
    ~PadOverviewWidget() override;

    // ── Widget identity ──────────────────────────────────────────────────
    WidgetDescriptor describe() const override;
    juce::String getTitle() const override { return "Pad Overview"; }

    // ── Lifecycle ────────────────────────────────────────────────────────
    void onActivated(int panelOffset) override;
    void onDeactivated() override;
    void onActiveSamplerAboutToChange() override;
    void onActiveSamplerChanged() override;

    // ── Resources ────────────────────────────────────────────────────────
    std::vector<std::string> requiredResources(int page) override;

    // ── Options & Knobs ──────────────────────────────────────────────────
    std::vector<Option> getOptions(int page) override;
    std::vector<Knob> getKnobs(int page) override;

    // ── Rendering ────────────────────────────────────────────────────────
    void paintPage(juce::Graphics& g, int page, juce::Rectangle<int> bounds) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    // ── Input ────────────────────────────────────────────────────────────
    void handlePad(const controller_events::PadEvent& e) override;
    void handleButton(const controller_events::ButtonEvent& e) override;

    // ── SamplerInstrument::Listener ──────────────────────────────────────
    void padTriggered(int padIndex) override;

    /** Set a callback invoked when a pad is selected (Select+Pad). */
    void setPadSelectedCallback(std::function<void(int)> callback);

    void onUiHostTick() override;

private:
    // ── Refresh helpers ──────────────────────────────────────────────────

    /** Rebuild padSnapshots_ + thumbnails; calls refreshTiles(). */
    void refreshSnapshots();

    /** Build the Tile array and push to grid_. Also publishes LEDs
        (via PadGridComponent's LED publishing path). */
    void refreshTiles();

    void pruneExpiredFlashes();
    void handlePadTriggered(int padId);
    void updateThumbnailForPad(int padIndex, const SamplerInstrument::PadSnapshot& pad);

    // ── State ────────────────────────────────────────────────────────────

    std::vector<SamplerInstrument::PadSnapshot> padSnapshots_;
    std::unordered_map<int, double> padFlashTimes_;
    bool listeningForPadTriggers_ { false };

    // Waveform thumbnails
    juce::AudioFormatManager thumbnailFormatManager_;
    juce::AudioThumbnailCache thumbnailCache_ { 16 };
    std::unordered_map<int, std::unique_ptr<juce::AudioThumbnail>> padThumbnails_;
    std::unordered_map<int, juce::String> loadedSamplePaths_;
    std::function<void(int)> padSelectedCallback_;

    // ── Child components ─────────────────────────────────────────────────

    PadGridComponent grid_;

    // ── Mute/solo hold workflow ──────────────────────────────────────────

    static constexpr std::uint32_t kInvalidBinding = 0;
    bool muteHeld_ { false };
    bool soloHeld_ { false };
    std::uint32_t padHoldBinding_ { kInvalidBinding };
    void updateModifierLeds();
    void releasePadHoldBinding();

    // ── Constants ────────────────────────────────────────────────────────

    static constexpr double kFlashDurationMs = 220.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadOverviewWidget)
};
