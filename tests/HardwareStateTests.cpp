#include <gtest/gtest.h>

#include "../src/ui/hw/HardwareState.h"

namespace
{

/** Mock FlushTarget for testing flush(). */
class MockFlushTarget : public HardwareState::FlushTarget
{
public:
    void beginLedBatch() override
    {
        ++batchBeginCount;
    }

    void endLedBatch() override
    {
        ++batchEndCount;
    }

    void setMonoLed(const std::string& name, uint8_t brightness) override
    {
        monoLeds[name] = brightness;
    }

    void setIndexedLed(const std::string& name, uint8_t colorIndex) override
    {
        indexedLeds[name] = colorIndex;
    }

    void setButtonBrightness(const std::string& name, uint8_t brightness) override
    {
        buttonLeds[name] = brightness;
    }

    std::unordered_map<std::string, uint8_t> monoLeds;
    std::unordered_map<std::string, uint8_t> indexedLeds;
    std::unordered_map<std::string, uint8_t> buttonLeds;
    int batchBeginCount { 0 };
    int batchEndCount { 0 };
};

class HardwareStateTest : public ::testing::Test
{
protected:
    HardwareState hw;
};

// 1. Fresh state — all resources unclaimed, getLed returns 0
TEST_F(HardwareStateTest, FreshStateAllUnclaimed)
{
    EXPECT_FALSE(hw.isClaimed("p1"));
    EXPECT_FALSE(hw.isClaimed("g1"));
    EXPECT_FALSE(hw.isClaimed("d1"));
    EXPECT_FALSE(hw.isClaimed("k1"));
    EXPECT_FALSE(hw.isClaimed("play"));
    EXPECT_FALSE(hw.isClaimed("ts1"));
    EXPECT_FALSE(hw.isClaimed("navUp"));

    EXPECT_EQ(hw.getLed("p1"), 0);
    EXPECT_EQ(hw.getLed("g8"), 0);
    EXPECT_EQ(hw.getOwner("p1"), "");
}

// 2. claim/release cycle
TEST_F(HardwareStateTest, ClaimReleaseCycle)
{
    hw.claim("p1", "widgetA");
    EXPECT_TRUE(hw.isClaimed("p1"));
    EXPECT_EQ(hw.getOwner("p1"), "widgetA");

    hw.release("p1", "widgetA");
    EXPECT_FALSE(hw.isClaimed("p1"));
    EXPECT_EQ(hw.getOwner("p1"), "");
}

// 3. Double-claim throws with descriptive message
TEST_F(HardwareStateTest, DoubleClaimThrows)
{
    hw.claim("p1", "widgetA");

    try
    {
        hw.claim("p1", "widgetB");
        FAIL() << "Expected std::runtime_error";
    }
    catch (const std::runtime_error& e)
    {
        std::string msg = e.what();
        EXPECT_NE(msg.find("p1"), std::string::npos);
        EXPECT_NE(msg.find("widgetA"), std::string::npos);
        EXPECT_NE(msg.find("widgetB"), std::string::npos);
    }
}

// 4. Same-owner re-claim is no-op
TEST_F(HardwareStateTest, SameOwnerReClaimNoOp)
{
    hw.claim("p1", "widgetA");
    EXPECT_NO_THROW(hw.claim("p1", "widgetA"));
    EXPECT_EQ(hw.getOwner("p1"), "widgetA");
}

// 5. Release wrong owner throws
TEST_F(HardwareStateTest, ReleaseWrongOwnerThrows)
{
    hw.claim("p1", "widgetA");
    EXPECT_THROW(hw.release("p1", "widgetB"), std::runtime_error);
}

// 6. claimSet/releaseSet
TEST_F(HardwareStateTest, ClaimSetReleaseSet)
{
    hw.claimSet(ResourceSet::PadLeds, "widgetA");

    for (const auto& id : ResourceIds::PadLeds)
    {
        EXPECT_TRUE(hw.isClaimed(id)) << "Expected " << id << " to be claimed";
        EXPECT_EQ(hw.getOwner(id), "widgetA");
    }

    hw.releaseSet(ResourceSet::PadLeds, "widgetA");

    for (const auto& id : ResourceIds::PadLeds)
    {
        EXPECT_FALSE(hw.isClaimed(id)) << "Expected " << id << " to be unclaimed";
    }
}

// 7. setLed ownership check
TEST_F(HardwareStateTest, SetLedOwnershipCheck)
{
    // setLed on unclaimed is a no-op
    hw.setLed("p1", 127, "widgetA");
    EXPECT_EQ(hw.getLed("p1"), 0);

    // setLed by wrong owner is a no-op
    hw.claim("p1", "widgetA");
    hw.setLed("p1", 127, "widgetB");
    EXPECT_EQ(hw.getLed("p1"), 0);

    // setLed by owner succeeds
    hw.setLed("p1", 127, "widgetA");
    EXPECT_EQ(hw.getLed("p1"), 127);
}

// 8. Dirty tracking
TEST_F(HardwareStateTest, DirtyTracking)
{
    // Initially no dirty resources
    EXPECT_TRUE(hw.getDirtyResources().empty());

    hw.claim("p1", "widgetA");
    hw.setLed("p1", 42, "widgetA");

    auto dirty = hw.getDirtyResources();
    ASSERT_EQ(dirty.size(), 1u);
    EXPECT_EQ(dirty[0].first, "p1");
    EXPECT_EQ(dirty[0].second, 42);

    hw.clearDirty();
    EXPECT_TRUE(hw.getDirtyResources().empty());
}

// 9. releaseAll
TEST_F(HardwareStateTest, ReleaseAll)
{
    hw.claimSet(ResourceSet::PadLeds, "widgetA");
    hw.claimSet(ResourceSet::GroupLeds, "widgetA");
    hw.claim("play", "widgetB");

    hw.releaseAll("widgetA");

    for (const auto& id : ResourceIds::PadLeds)
        EXPECT_FALSE(hw.isClaimed(id));

    for (const auto& id : ResourceIds::GroupLeds)
        EXPECT_FALSE(hw.isClaimed(id));

    // widgetB's resource should be untouched
    EXPECT_TRUE(hw.isClaimed("play"));
    EXPECT_EQ(hw.getOwner("play"), "widgetB");
}

// 10. forceRelease
TEST_F(HardwareStateTest, ForceRelease)
{
    hw.claim("p1", "widgetA");
    EXPECT_TRUE(hw.isClaimed("p1"));

    hw.forceRelease("p1");
    EXPECT_FALSE(hw.isClaimed("p1"));
    EXPECT_EQ(hw.getOwner("p1"), "");
}

// Unknown resource throws
TEST_F(HardwareStateTest, UnknownResourceThrows)
{
    EXPECT_THROW(hw.claim("nonexistent", "widgetA"), std::runtime_error);
    EXPECT_THROW(hw.getLed("nonexistent"), std::runtime_error);
}

// 11. Screen button resources exist
TEST_F(HardwareStateTest, ScreenButtonResourcesExist)
{
    EXPECT_FALSE(hw.isClaimed("pattern"));
    EXPECT_FALSE(hw.isClaimed("mixer"));
    EXPECT_FALSE(hw.isClaimed("sampling"));
    EXPECT_FALSE(hw.isClaimed("arranger"));
    EXPECT_FALSE(hw.isClaimed("step"));
}

// --- Flush tests ---

TEST_F(HardwareStateTest, FlushSendsDirtyMonoLed)
{
    MockFlushTarget target;

    hw.claim("play", "transport");
    hw.setLed("play", 63, "transport");

    hw.flush(target);

    EXPECT_EQ(target.monoLeds["play"], 63);
    EXPECT_TRUE(hw.getDirtyResources().empty());
}

TEST_F(HardwareStateTest, FlushSendsDirtyIndexedLed)
{
    MockFlushTarget target;

    hw.claim("p1", "widgetA");
    hw.setLed("p1", 42, "widgetA");

    hw.flush(target);

    EXPECT_EQ(target.indexedLeds["p1"], 42);
    EXPECT_TRUE(hw.getDirtyResources().empty());
}

TEST_F(HardwareStateTest, FlushSkipsCleanResources)
{
    MockFlushTarget target;

    hw.claim("play", "transport");
    hw.setLed("play", 63, "transport");
    hw.clearDirty();

    hw.flush(target);

    EXPECT_TRUE(target.monoLeds.empty());
    EXPECT_TRUE(target.indexedLeds.empty());
    EXPECT_EQ(target.batchBeginCount, 0);
    EXPECT_EQ(target.batchEndCount, 0);
}

TEST_F(HardwareStateTest, RepeatedValueStaysCleanAfterFlush)
{
    MockFlushTarget target;

    hw.claim("play", "transport");
    hw.setLed("play", 63, "transport");
    hw.flush(target);
    target.monoLeds.clear();

    hw.setLed("play", 63, "transport");
    hw.flush(target);

    EXPECT_TRUE(target.monoLeds.empty());
    EXPECT_TRUE(hw.getDirtyResources().empty());
}

TEST_F(HardwareStateTest, ExplicitRefreshResendsUnchangedValue)
{
    MockFlushTarget target;

    hw.claim("play", "transport");
    hw.setLed("play", 63, "transport");
    hw.flush(target);
    target.monoLeds.clear();

    hw.markSetDirty(ResourceSet::TransportLeds);
    hw.flush(target);

    EXPECT_EQ(target.monoLeds["play"], 63);
}

TEST_F(HardwareStateTest, FlushSendsMultipleDirtyResources)
{
    MockFlushTarget target;

    hw.claim("play", "transport");
    hw.claim("p1", "widgetA");
    hw.setLed("play", 32, "transport");
    hw.setLed("p1", 10, "widgetA");

    hw.flush(target);

    EXPECT_EQ(target.monoLeds["play"], 32);
    EXPECT_EQ(target.indexedLeds["p1"], 10);
    EXPECT_EQ(target.batchBeginCount, 1);
    EXPECT_EQ(target.batchEndCount, 1);
}

TEST_F(HardwareStateTest, ReleaseMakesDirtyWhenValueNonZero)
{
    hw.claim("p1", "widgetA");
    hw.setLed("p1", 42, "widgetA");
    hw.clearDirty();

    hw.release("p1", "widgetA");

    auto dirty = hw.getDirtyResources();
    ASSERT_EQ(dirty.size(), 1u);
    EXPECT_EQ(dirty[0].second, 0);
}

TEST_F(HardwareStateTest, ReleaseNotDirtyWhenValueAlreadyZero)
{
    hw.claim("p1", "widgetA");
    hw.clearDirty();

    hw.release("p1", "widgetA");
    EXPECT_TRUE(hw.getDirtyResources().empty());
}

TEST_F(HardwareStateTest, ReleaseAllFlushesOffState)
{
    MockFlushTarget target;

    hw.claimSet(ResourceSet::PadLeds, "widgetA");
    for (int i = 1; i <= 16; ++i)
        hw.setLed("p" + std::to_string(i), 42, "widgetA");
    hw.clearDirty();

    hw.releaseAll("widgetA");
    hw.flush(target);

    for (int i = 1; i <= 16; ++i)
        EXPECT_EQ(target.indexedLeds["p" + std::to_string(i)], 0);
}

TEST_F(HardwareStateTest, GroupLedsAreIndexedType)
{
    MockFlushTarget target;

    hw.claim("g1", "groups");
    hw.setLed("g1", 55, "groups");
    hw.flush(target);

    EXPECT_EQ(target.indexedLeds["g1"], 55);
    EXPECT_TRUE(target.monoLeds.empty());
}

TEST_F(HardwareStateTest, ScreenButtonsAreButtonType)
{
    MockFlushTarget target;

    hw.claim("pattern", "patternWidget");
    hw.setLed("pattern", 63, "patternWidget");
    hw.flush(target);

    EXPECT_EQ(target.buttonLeds["pattern"], 63);
    EXPECT_TRUE(target.monoLeds.empty());
    EXPECT_TRUE(target.indexedLeds.empty());
}

} // namespace
