#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../hw/HardwareState.h"
#include "../theme/UiTheme.h"

/**
 * PadGridComponent — data-driven 4×4 pad tile grid.
 *
 * Paints a 4×4 grid of coloured tiles with optional labels and an
 * overlay callback for widget-specific content (e.g. waveform thumbnails).
 *
 * Pad index convention (matches Maschine Mk3 logical layout):
 *   Index 0 = bottom-left, 15 = top-right
 *   row = 3 - (index / 4)
 *   col = index % 4
 *
 * Usage:
 *   1. Construct and add as a child component.
 *   2. Call setStyle() once to configure visual parameters.
 *   3. Optionally call enableLedPublishing() to automatically write LED
 *      colors from Tile::ledColor on each setTiles() call.
 *   4. Build a std::array<Tile, 16> and call setTiles() every render tick.
 */
class PadGridComponent : public juce::Component
{
public:
    // ── Data types ────────────────────────────────────────────────────────

    struct Tile
    {
        juce::Colour fill { juce::Colours::transparentBlack };
        juce::String label;
        juce::Font labelFont { juce::FontOptions(12.0f, juce::Font::bold) };
        juce::Colour labelColour { juce::Colours::white };
        uint8_t ledColor { 0 };  // Only used when enableLedPublishing() is active; otherwise ignored.
    };

    struct Style
    {
        float borderWidth { 1.0f };
        float cornerRadius { 0.0f };
        juce::Colour borderColour { UiTheme::kPadBorder };
        int gapPx { 2 };
    };

    /** Called after each tile's base paint — use for pixel-level content
        (waveform thumbnails, icons). For solid-colour tiles, set Tile::fill
        instead. Signature: (Graphics&, tileBounds, padIndex). */
    using OverlayFn = std::function<void(juce::Graphics&, juce::Rectangle<float>, int)>;

    // ── Construction ─────────────────────────────────────────────────────

    PadGridComponent();
    ~PadGridComponent() override;

    // ── Configuration ─────────────────────────────────────────────────────

    /** Safe to call once at construction; style does not reset on deactivation. */
    void setStyle(const Style& s);

    /** Replace the tile array. Triggers repaint + requestUiRefresh.
        Also publishes LEDs if enableLedPublishing() was called. */
    void setTiles(const std::array<Tile, 16>& tiles);

    /** Optional overlay drawn on top of each tile after base paint. */
    void setTileOverlay(OverlayFn fn);

    /** Automatically publish LED colors to HardwareState on every setTiles().
        Widgets with custom LED logic can skip this and call hw().setLed() themselves.
        ownerId is stored as std::string to match HardwareState's API — pass
        `describe().id.toStdString()` from a Widget. */
    void enableLedPublishing(HardwareState& hw, std::string ownerId);

    /** Clear the cached HardwareState pointer. Widgets should call this in
        onDeactivated() so a later setTiles() doesn't dereference a possibly-
        dangling reference. */
    void disableLedPublishing();

    // ── Hit testing ──────────────────────────────────────────────────────

    /** Returns pad index (0-15) at the given local point, or -1 if none. */
    int padIndexAt(juce::Point<int> localPoint) const;

    /** Returns cached tile bounds for the given pad index. */
    juce::Rectangle<float> getTileBounds(int padIndex) const;

    // ── Component overrides ───────────────────────────────────────────────

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int kCols = 4;
    static constexpr int kRows = 4;
    static constexpr int kTileCount = kCols * kRows;

    std::array<Tile, kTileCount> tiles_;
    Style style_;
    OverlayFn overlay_;

    HardwareState* hw_ { nullptr };
    std::string ledOwnerId_;

    std::array<juce::Rectangle<float>, kTileCount> tileBounds_;

    void recalcTileBounds();
    void publishLedsIfEnabled();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadGridComponent)
};
