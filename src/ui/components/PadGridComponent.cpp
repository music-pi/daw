#include "PadGridComponent.h"

#include "../UiRefresh.h"

// ── Construction ─────────────────────────────────────────────────────────────

PadGridComponent::PadGridComponent()
{
    // Initialise tile bounds to zero-size rectangles
    tileBounds_.fill(juce::Rectangle<float>());
}

PadGridComponent::~PadGridComponent() = default;

// ── Configuration ─────────────────────────────────────────────────────────────

void PadGridComponent::setStyle(const Style& s)
{
    style_ = s;
    recalcTileBounds();
    repaint();
}

void PadGridComponent::setTiles(const std::array<Tile, kTileCount>& tiles)
{
    tiles_ = tiles;
    publishLedsIfEnabled();
    repaint();
    requestUiRefresh(*this);
}

void PadGridComponent::setTileOverlay(OverlayFn fn)
{
    overlay_ = std::move(fn);
    repaint();
}

void PadGridComponent::enableLedPublishing(HardwareState& hw, std::string ownerId)
{
    hw_         = &hw;
    ledOwnerId_ = std::move(ownerId);
}

void PadGridComponent::disableLedPublishing()
{
    hw_ = nullptr;
    ledOwnerId_.clear();
}

// ── Hit testing ──────────────────────────────────────────────────────────────

int PadGridComponent::padIndexAt(juce::Point<int> localPoint) const
{
    for (int i = 0; i < kTileCount; ++i)
    {
        if (tileBounds_[static_cast<size_t>(i)].contains(localPoint.toFloat()))
            return i;
    }
    return -1;
}

juce::Rectangle<float> PadGridComponent::getTileBounds(int padIndex) const
{
    if (padIndex < 0 || padIndex >= kTileCount)
        return {};
    return tileBounds_[static_cast<size_t>(padIndex)];
}

// ── Component overrides ───────────────────────────────────────────────────────

void PadGridComponent::paint(juce::Graphics& g)
{
    for (int i = 0; i < kTileCount; ++i)
    {
        const auto& tile   = tiles_[static_cast<size_t>(i)];
        const auto& bounds = tileBounds_[static_cast<size_t>(i)];

        if (bounds.isEmpty())
            continue;

        // Fill
        g.setColour(tile.fill);
        if (style_.cornerRadius > 0.0f)
            g.fillRoundedRectangle(bounds, style_.cornerRadius);
        else
            g.fillRect(bounds);

        // Border
        g.setColour(style_.borderColour);
        if (style_.cornerRadius > 0.0f)
            g.drawRoundedRectangle(bounds, style_.cornerRadius, style_.borderWidth);
        else
            g.drawRect(bounds, style_.borderWidth);

        // Overlay (e.g. waveform thumbnails) — drawn before text so label is on top
        if (overlay_)
            overlay_(g, bounds, i);

        // Label
        if (tile.label.isNotEmpty())
        {
            g.setFont(tile.labelFont);
            g.setColour(tile.labelColour);
            g.drawFittedText(tile.label,
                             bounds.toNearestInt().reduced(2),
                             juce::Justification::centred,
                             2);
        }
    }
}

void PadGridComponent::resized()
{
    recalcTileBounds();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void PadGridComponent::recalcTileBounds()
{
    const auto area  = getLocalBounds().toFloat();
    const float cellW = area.getWidth()  / static_cast<float>(kCols);
    const float cellH = area.getHeight() / static_cast<float>(kRows);
    const float half  = static_cast<float>(style_.gapPx) * 0.5f;

    for (int index = 0; index < kTileCount; ++index)
    {
        const int row = 3 - (index / kCols);  // 0 = bottom → displayed at bottom
        const int col = index % kCols;

        juce::Rectangle<float> cell(
            area.getX() + static_cast<float>(col)       * cellW,
            area.getY() + static_cast<float>(row)       * cellH,
            cellW,
            cellH);

        // Shrink each cell by half the gap on all sides — adjacent cells then
        // have a full gapPx gap between their filled areas.
        tileBounds_[static_cast<size_t>(index)] = cell.reduced(half);
    }
}

void PadGridComponent::publishLedsIfEnabled()
{
    if (hw_ == nullptr)
        return;

    for (int i = 0; i < kTileCount; ++i)
    {
        const auto& resource = ResourceIds::PadLeds[static_cast<size_t>(i)];
        // A sibling panel may temporarily own the physical pads (for example
        // Pattern step mode beside Pad Overview). Keep rendering the grid but
        // do not write through another widget's hardware lease.
        if (hw_->getOwner(resource) != ledOwnerId_)
            continue;
        hw_->setLed(resource,
                    tiles_[static_cast<size_t>(i)].ledColor,
                    ledOwnerId_);
    }
}
