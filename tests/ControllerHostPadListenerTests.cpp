#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"
#include "harness/JuceHarness.h"

#include <chrono>

namespace
{

TEST(ControllerHostPadListenerDispatch, RegistersAndDispatchesManyListeners)
{
    testharness::JuceFrameworkContext jctx;

    ControllerHost host(nullptr, /*enableHardware=*/false);

    // Smaller scale so the async-post cost (MessageManager::callAsync per
    // listener per event) doesn't dominate and mask what the fix actually
    // saves — a heap-allocating vector copy per event.
    constexpr int kListenerCount = 100;
    constexpr int kDispatches    = 100;

    std::vector<ControllerHost::PadListenerId> ids;
    ids.reserve(kListenerCount);
    for (int i = 0; i < kListenerCount; ++i)
        ids.push_back(host.addPadListener([](const ControllerHost::PadEvent&) {}));

    // Regression guard; not a tight perf contract — loose bound avoids CI
    // flakes. The in-place iteration fix saves a full vector copy per event;
    // each async post itself costs more than a small-vector copy, so the
    // bound only flags catastrophic regressions.
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kDispatches; ++i)
    {
        ControllerHost::PadEvent event{ static_cast<uint8_t>(i % 16), true, 512, false, false };
        host.handlePadEvent(event);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    EXPECT_LT(ms, 500) << "Pad listener dispatch took " << ms << "ms (catastrophic regression?)";

    for (auto id : ids)
        host.removePadListener(id);
}

} // namespace
