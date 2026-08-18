#include <gtest/gtest.h>

#include "../src/ui/components/Toast.h"

namespace
{

// Arbitrary fixed timestamp used as "now" in tests.
constexpr int64_t kNow = 1000000;

// ── Toast.Info_ShowsWithDefaultDuration ──────────────────────────────────────

TEST(Toast, Info_ShowsWithDefaultDuration)
{
    ToastComponent toast;
    EXPECT_FALSE(toast.isShowing());

    toast.show(ToastKind::Info, "Hello", 0, kNow);

    EXPECT_TRUE(toast.isShowing());
    EXPECT_EQ(toast.getKind(), ToastKind::Info);
    EXPECT_EQ(toast.getMessage(), "Hello");
}

// ── Toast.Loading_Indeterminate_NotDismissedByTimer ───────────────────────────

TEST(Toast, Loading_Indeterminate_NotDismissedByTimer)
{
    ToastComponent toast;
    toast.show(ToastKind::Loading, "Saving...", 0, kNow);
    EXPECT_TRUE(toast.isShowing());

    // Tick far into the future — Loading should never auto-dismiss.
    toast.tick(kNow + 999999);
    EXPECT_TRUE(toast.isShowing());
    EXPECT_EQ(toast.getKind(), ToastKind::Loading);
}

// ── Toast.Priority_InfoDoesNotReplaceLoading ──────────────────────────────────

TEST(Toast, Priority_InfoDoesNotReplaceLoading)
{
    ToastComponent toast;
    toast.show(ToastKind::Loading, "Saving...", 0, kNow);
    EXPECT_TRUE(toast.isShowing());

    // Info (priority 0) should NOT replace Loading (priority 2)
    uint64_t id = toast.show(ToastKind::Info, "BPM 120", 0, kNow);
    EXPECT_EQ(id, 0u); // dropped (0 = null id)
    EXPECT_EQ(toast.getKind(), ToastKind::Loading);
    EXPECT_EQ(toast.getMessage(), "Saving...");
}

// ── Toast.Priority_ErrorReplacesLoading ──────────────────────────────────────

TEST(Toast, Priority_ErrorReplacesLoading)
{
    ToastComponent toast;
    toast.show(ToastKind::Loading, "Saving...", 0, kNow);
    EXPECT_EQ(toast.getKind(), ToastKind::Loading);

    // Error (priority 4) should replace Loading (priority 2)
    uint64_t id = toast.show(ToastKind::Error, "Save failed!", 0, kNow);
    EXPECT_GT(id, 0u);
    EXPECT_EQ(toast.getKind(), ToastKind::Error);
    EXPECT_EQ(toast.getMessage(), "Save failed!");
}

// ── Toast.Handle_DropDismissesLoading ─────────────────────────────────────────

TEST(Toast, Handle_DropDismissesLoading)
{
    ToastComponent toast;
    uint64_t id = toast.show(ToastKind::Loading, "Working…", 0, kNow);
    EXPECT_GT(id, 0u);
    EXPECT_TRUE(toast.isShowing());

    {
        ToastHandle handle(juce::WeakReference<ToastComponent>(&toast), id);
        EXPECT_TRUE(handle.isOwned());
        // handle goes out of scope here → destructor calls dismiss()
    }

    EXPECT_FALSE(toast.isShowing());
}

// ── Toast.Handle_ResolveReplacesWithFinalKind ─────────────────────────────────

TEST(Toast, Handle_ResolveReplacesWithFinalKind)
{
    ToastComponent toast;
    uint64_t id = toast.show(ToastKind::Loading, "Saving...", 0, kNow);
    ToastHandle handle(juce::WeakReference<ToastComponent>(&toast), id);

    EXPECT_TRUE(handle.isOwned());
    handle.resolve(ToastKind::Success, "Saved!");

    EXPECT_TRUE(toast.isShowing());
    EXPECT_EQ(toast.getKind(), ToastKind::Success);
    EXPECT_EQ(toast.getMessage(), "Saved!");
    EXPECT_FALSE(handle.isOwned()); // handle relinquished ownership after resolve
}

// ── Toast.Animation_LoadingAdvancesOnTick ─────────────────────────────────────

TEST(Toast, Animation_LoadingAdvancesOnTick)
{
    ToastComponent toast;
    toast.show(ToastKind::Loading, "Thinking...", 0, kNow);
    EXPECT_TRUE(toast.isShowing());

    // The animation phase is driven by (nowMs / 200) % 3.
    // At kNow=1000000: phase = (1000000/200) % 3 = 5000 % 3 = 2
    // At kNow+200:     phase = (1000200/200) % 3 = 5001 % 3 = 0
    // At kNow+400:     phase = (1000400/200) % 3 = 5002 % 3 = 1
    // At kNow+600:     phase = (1000600/200) % 3 = 5003 % 3 = 2  (wraps back)

    // Just verify the toast stays alive across several ticks (phase wrap doesn't dismiss it).
    toast.tick(kNow + 200);
    EXPECT_TRUE(toast.isShowing());
    toast.tick(kNow + 400);
    EXPECT_TRUE(toast.isShowing());
    toast.tick(kNow + 600);
    EXPECT_TRUE(toast.isShowing());
    toast.tick(kNow + 1000000); // far future — still alive (Loading)
    EXPECT_TRUE(toast.isShowing());
    EXPECT_EQ(toast.getKind(), ToastKind::Loading);
}

// ── Toast.EpochHandlesLongRun ─────────────────────────────────────────────────

TEST(Toast, EpochHandlesLongRun)
{
    // Guard rail for the uint64 handleId_ type change. Does not actually
    // exercise wraparound (would require 2^64 iterations) — instead shows
    // the counter monotonically advances through 100k show/dismiss cycles
    // with no pathology. If someone regresses the type back to int, this
    // still passes, but the point is to pin the contract in-test.
    ToastComponent toast;
    uint64_t last = 0;
    for (int i = 0; i < 100000; ++i)
    {
        uint64_t id = toast.show(ToastKind::Info, "x", 1, kNow);
        EXPECT_GT(id, last);
        last = id;
        toast.dismiss();
    }
    EXPECT_GE(last, 100000u);
}

} // namespace
