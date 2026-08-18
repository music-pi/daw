/**
 * UiHostDirtyGateTests.cpp
 *
 * Verifies the display-dirty-gate pattern added to UiHost::timerCallback.
 *
 * Constraint: UiHost cannot be instantiated in the test harness because its
 * constructor calls juce::JUCEApplication::getInstance() and casts it to App*,
 * which requires a live JUCEApplication singleton not present in unit tests.
 *
 * Instead, these tests verify:
 *   1. MockControllerHost::pushDisplayFrame counter increments correctly.
 *   2. A self-contained DirtyGateTimer shim (same atomic mask + exchange pattern as
 *      UiHost::timerCallback) demonstrates correct gating semantics.
 *
 * The DirtyGateTimer shim faithfully reproduces the gating logic from
 * UiHost::timerCallback:
 *   - displayDirtyMask_ is initialised to both panels (same as UiHost)
 *   - timerCallback does exchange(0) and skips the push when already clean
 *   - requests OR panel bits so concurrent invalidations are preserved
 */

#include <atomic>
#include <gtest/gtest.h>

#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

// ---------------------------------------------------------------------------
// Minimal shim that reproduces UiHost's dirty-gate logic
// ---------------------------------------------------------------------------
namespace
{

class DirtyGateTimer
{
public:
    explicit DirtyGateTimer(testharness::MockControllerHost& ctrl)
        : ctrl_(ctrl)
    {
    }

    /** Simulate one timerCallback firing. */
    void tick()
    {
        // Gate: skip frame push when display is clean
        const unsigned dirty = displayDirtyMask_.exchange(0u, std::memory_order_acq_rel);
        if (dirty == 0u)
            return;

        juce::Image dummy(juce::Image::ARGB, 480, 272, true);
        if ((dirty & 1u) != 0u)
            ctrl_.pushDisplayFrame(0, dummy);
        if ((dirty & 2u) != 0u)
            ctrl_.pushDisplayFrame(1, dummy);
    }

    /** Equivalent to UiHost::requestDisplayRefresh(). */
    void requestDisplayRefresh() noexcept
    {
        requestDisplayRefresh(3u);
    }

    void requestDisplayRefresh(unsigned panelMask) noexcept
    {
        displayDirtyMask_.fetch_or(panelMask & 3u, std::memory_order_release);
    }

private:
    testharness::MockControllerHost& ctrl_;
    std::atomic<unsigned> displayDirtyMask_ { 3u };
};

} // namespace

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class UiHostDirtyGateTests : public ::testing::Test
{
protected:
    testharness::JuceFrameworkContext juceContext;
    testharness::MockControllerHost mockController;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/**
 * On the first tick the display is dirty (initial value = true), so
 * pushDisplayFrame should be called for both panels (count >= 2).
 */
TEST_F(UiHostDirtyGateTests, FirstTickPushesFrames)
{
    DirtyGateTimer timer(mockController);

    timer.tick();

    EXPECT_GE(mockController.pushDisplayFrameCount(), 2)
        << "Initial dirty=true should push both left and right panels on first tick";
}

/**
 * After the first tick clears the dirty flag, subsequent ticks without a
 * refresh request must NOT push any frames.
 */
TEST_F(UiHostDirtyGateTests, SkipsPushWhenClean)
{
    DirtyGateTimer timer(mockController);

    // First tick: dirty=true, pushes frames and clears flag
    timer.tick();
    mockController.resetCounters();

    // Second and third ticks: dirty=false, no push
    timer.tick();
    timer.tick();

    EXPECT_EQ(mockController.pushDisplayFrameCount(), 0)
        << "Subsequent ticks with no refresh request must not push frames";
}

/**
 * After requestDisplayRefresh() marks the display dirty again, the very next
 * tick must push frames (both panels).
 */
TEST_F(UiHostDirtyGateTests, RefreshRequestEnablesNextPush)
{
    DirtyGateTimer timer(mockController);

    // Consume the initial dirty flag
    timer.tick();
    mockController.resetCounters();

    // Ticks while clean: no push
    timer.tick();
    ASSERT_EQ(mockController.pushDisplayFrameCount(), 0);

    // Mark dirty again and verify the next tick pushes
    timer.requestDisplayRefresh();
    timer.tick();

    EXPECT_GE(mockController.pushDisplayFrameCount(), 2)
        << "A refresh request must cause the next tick to push both panels";
}

/**
 * Verify that a second requestDisplayRefresh() after a push cycle re-arms
 * exactly one more push (not two).
 */
TEST_F(UiHostDirtyGateTests, SinglePushPerDirtyRequest)
{
    DirtyGateTimer timer(mockController);

    // Consume initial dirty
    timer.tick();
    mockController.resetCounters();

    // Request once, fire multiple ticks
    timer.requestDisplayRefresh();
    timer.tick(); // should push
    timer.tick(); // should NOT push (flag already consumed)
    timer.tick(); // should NOT push

    EXPECT_EQ(mockController.pushDisplayFrameCount(), 2)
        << "A single refresh request should produce exactly one push cycle (2 frame calls)";
}

TEST_F(UiHostDirtyGateTests, LeftOnlyRequestPushesOnlyLeftPanel)
{
    DirtyGateTimer timer(mockController);
    timer.tick();
    mockController.resetCounters();

    timer.requestDisplayRefresh(1u);
    timer.tick();

    ASSERT_EQ(mockController.pushedDisplayScreens().size(), 1u);
    EXPECT_EQ(mockController.pushedDisplayScreens().front(), 0);
}

TEST_F(UiHostDirtyGateTests, PanelRequestsCoalesceWithoutLosingEitherSide)
{
    DirtyGateTimer timer(mockController);
    timer.tick();
    mockController.resetCounters();

    timer.requestDisplayRefresh(1u);
    timer.requestDisplayRefresh(2u);
    timer.tick();

    EXPECT_EQ(mockController.pushedDisplayScreens(), (std::vector<int> { 0, 1 }));
}

/**
 * Verify MockControllerHost::resetCounters() works correctly so tests are
 * independent.
 */
TEST_F(UiHostDirtyGateTests, ResetCountersWorks)
{
    DirtyGateTimer timer(mockController);

    timer.tick();
    EXPECT_GE(mockController.pushDisplayFrameCount(), 2);

    mockController.resetCounters();
    EXPECT_EQ(mockController.pushDisplayFrameCount(), 0);
}
