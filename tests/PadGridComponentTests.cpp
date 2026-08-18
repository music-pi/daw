#include <gtest/gtest.h>

#include "../src/ui/components/PadGridComponent.h"
#include "../src/ui/hw/HardwareState.h"
#include "harness/JuceHarness.h"

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

class PadGridComponentTests : public ::testing::Test
{
protected:
    testharness::JuceFrameworkContext juceContext;
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. padIndexAt — logical layout correctness
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PadGridComponentTests, PadIndexAtReturnsCorrectIndex)
{
    PadGridComponent grid;
    grid.setBounds(0, 0, 400, 400);
    grid.resized();

    // Each cell is 100×100 px.
    // Index convention: 0 = bottom-left, 15 = top-right.
    //
    // Row 3 (top display row, y=0..100)    → indices 12,13,14,15
    // Row 2 (y=100..200)                   → indices  8, 9,10,11
    // Row 1 (y=200..300)                   → indices  4, 5, 6, 7
    // Row 0 (bottom display row,y=300..400)→ indices  0, 1, 2, 3
    //
    // Column: col 0 (x=0..100)=left … col 3 (x=300..400)=right

    // Bottom-left cell centre → pad 0
    EXPECT_EQ(grid.padIndexAt({ 50, 350 }), 0);

    // Top-right cell centre → pad 15
    EXPECT_EQ(grid.padIndexAt({ 350, 50 }), 15);

    // Bottom-right cell centre → pad 3
    EXPECT_EQ(grid.padIndexAt({ 350, 350 }), 3);

    // Top-left cell centre → pad 12
    EXPECT_EQ(grid.padIndexAt({ 50, 50 }), 12);

    // Mid-grid: row=1 (display), col=1 → pad 5
    // Display row 1 corresponds to index row 2: 2*4+1 = 9
    // Actually: display row 0 (top) = logical row 3, display row 1 = logical row 2
    // index = (3 - displayRow) * 4 + col
    // pad at display row=2, col=1: index = (3-2)*4+1 = 5
    EXPECT_EQ(grid.padIndexAt({ 150, 250 }), 5);

    // Pad 10: col=2, logical row=2 → display row=1 → y=100..200, x=200..300
    EXPECT_EQ(grid.padIndexAt({ 250, 150 }), 10);

    // Outside bounds → -1
    EXPECT_EQ(grid.padIndexAt({ -10, 200 }), -1);
    EXPECT_EQ(grid.padIndexAt({ 500, 200 }), -1);
    EXPECT_EQ(grid.padIndexAt({ 200, 500 }), -1);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. setTiles — triggers repaint (smoke test, just verifies no crash)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PadGridComponentTests, SetTilesNocrash)
{
    PadGridComponent grid;
    grid.setBounds(0, 0, 400, 400);

    std::array<PadGridComponent::Tile, 16> tiles;
    tiles[0].fill   = juce::Colours::red;
    tiles[0].label  = "test";

    // Must not throw or crash
    EXPECT_NO_THROW(grid.setTiles(tiles));
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. enableLedPublishing — writes LED colors to HardwareState
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PadGridComponentTests, EnableLedPublishingWritesToHardwareState)
{
    HardwareState hw;

    // Claim all pad LEDs so setLed() won't throw ownership errors
    for (int i = 1; i <= 16; ++i)
        hw.claim("p" + std::to_string(i), "test_owner");

    PadGridComponent grid;
    grid.setBounds(0, 0, 400, 400);
    grid.enableLedPublishing(hw, "test_owner");

    std::array<PadGridComponent::Tile, 16> tiles;
    tiles[0].ledColor  = 42;
    tiles[1].ledColor  = 7;
    tiles[15].ledColor = 0;  // explicit off

    grid.setTiles(tiles);

    // Verify LED values were written to HardwareState
    EXPECT_EQ(hw.getLed("p1"),  42u);
    EXPECT_EQ(hw.getLed("p2"),  7u);
    EXPECT_EQ(hw.getLed("p16"), 0u);
}

TEST_F(PadGridComponentTests, EnableLedPublishingPublishesAllSixteen)
{
    HardwareState hw;
    for (int i = 1; i <= 16; ++i)
        hw.claim("p" + std::to_string(i), "owner");

    PadGridComponent grid;
    grid.setBounds(0, 0, 400, 400);
    grid.enableLedPublishing(hw, "owner");

    std::array<PadGridComponent::Tile, 16> tiles;
    for (int i = 0; i < 16; ++i)
        tiles[static_cast<size_t>(i)].ledColor = static_cast<uint8_t>(i + 10);

    grid.setTiles(tiles);

    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(hw.getLed(ResourceIds::PadLeds[static_cast<size_t>(i)]),
                  static_cast<uint8_t>(i + 10));
}

TEST_F(PadGridComponentTests, NoLedPublishingWhenNotEnabled)
{
    HardwareState hw;
    for (int i = 1; i <= 16; ++i)
        hw.claim("p" + std::to_string(i), "other");

    PadGridComponent grid;
    grid.setBounds(0, 0, 400, 400);
    // enableLedPublishing NOT called

    std::array<PadGridComponent::Tile, 16> tiles;
    tiles[0].ledColor = 55;

    // setTiles must not touch hw — all LEDs stay at 0 (initial value)
    EXPECT_NO_THROW(grid.setTiles(tiles));

    EXPECT_EQ(hw.getLed("p1"), 0u);
}

TEST_F(PadGridComponentTests, PublishingSkipsPadsLeasedBySiblingWidget)
{
    HardwareState hw;
    for (int i = 1; i <= 16; ++i)
        hw.claim("p" + std::to_string(i), "pattern");

    PadGridComponent grid;
    grid.enableLedPublishing(hw, "pad_overview");

    std::array<PadGridComponent::Tile, 16> tiles;
    tiles[0].ledColor = 55;

    EXPECT_NO_THROW(grid.setTiles(tiles));
    EXPECT_EQ(hw.getOwner("p1"), "pattern");
    EXPECT_EQ(hw.getLed("p1"), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Style affects layout (cornerRadius and borderWidth stored properly)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PadGridComponentTests, StyleAffectsBorderCornerRadius)
{
    PadGridComponent gridA;
    PadGridComponent gridB;

    gridA.setBounds(0, 0, 400, 400);
    gridB.setBounds(0, 0, 400, 400);

    PadGridComponent::Style styleA;
    styleA.cornerRadius = 0.0f;
    styleA.gapPx        = 2;

    PadGridComponent::Style styleB;
    styleB.cornerRadius = 3.0f;
    styleB.gapPx        = 4;

    gridA.setStyle(styleA);
    gridB.setStyle(styleB);

    // With gapPx=4 the cells should be smaller (more inset) than gapPx=2.
    // Verify via getTileBounds: gridB tiles are smaller.
    auto boundsA = gridA.getTileBounds(0);
    auto boundsB = gridB.getTileBounds(0);

    EXPECT_GT(boundsA.getWidth(),  boundsB.getWidth())  << "Larger gap should produce smaller tiles";
    EXPECT_GT(boundsA.getHeight(), boundsB.getHeight()) << "Larger gap should produce smaller tiles";
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. getTileBounds returns correct positions
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PadGridComponentTests, GetTileBoundsReturnsValidRects)
{
    PadGridComponent grid;
    grid.setBounds(0, 0, 400, 400);
    grid.resized();

    // Pad 0: bottom-left → display row 3 (y=300..400), col 0 (x=0..100)
    auto b0 = grid.getTileBounds(0);
    EXPECT_GT(b0.getY(), 200.0f);   // lower half
    EXPECT_LT(b0.getX(), 100.0f);  // left column

    // Pad 15: top-right → display row 0 (y=0..100), col 3 (x=300..400)
    auto b15 = grid.getTileBounds(15);
    EXPECT_LT(b15.getY(), 100.0f);   // upper area
    EXPECT_GT(b15.getX(), 200.0f);  // right column

    // Out-of-range returns empty
    EXPECT_TRUE(grid.getTileBounds(-1).isEmpty());
    EXPECT_TRUE(grid.getTileBounds(16).isEmpty());
}
