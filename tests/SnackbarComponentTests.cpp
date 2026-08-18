// These tests were originally for SnackbarComponent; they now cover the equivalent
// ToastComponent behaviour (same public surface: show, dismiss, isShowing, getMessage).
#include <gtest/gtest.h>

#include "../src/ui/components/Toast.h"

namespace
{

// Use an arbitrary fixed "now" so expiry arithmetic is deterministic.
constexpr int64_t kT0 = 2000000;

class SnackbarComponentTest : public ::testing::Test
{
protected:
    ToastComponent snackbar;
};

// 1. Initial state - not visible, isShowing() false
TEST_F(SnackbarComponentTest, InitialStateNotVisible)
{
    EXPECT_FALSE(snackbar.isShowing());
    EXPECT_FALSE(snackbar.isVisible());
}

// 2. show() - becomes visible, isShowing() true, displays correct message
TEST_F(SnackbarComponentTest, ShowMakesVisibleWithMessage)
{
    snackbar.show(ToastKind::Info, "Hello World", 5000, kT0);

    EXPECT_TRUE(snackbar.isShowing());
    EXPECT_TRUE(snackbar.isVisible());
    EXPECT_EQ(snackbar.getMessage(), "Hello World");
}

// 3. dismiss() - after calling dismiss(), isShowing() false
TEST_F(SnackbarComponentTest, DismissHidesSnackbar)
{
    snackbar.show(ToastKind::Info, "Test message", 5000, kT0);
    EXPECT_TRUE(snackbar.isShowing());

    snackbar.dismiss();
    EXPECT_FALSE(snackbar.isShowing());
}

// 4. Auto-dismiss - toast expires when tick is called past its duration
TEST_F(SnackbarComponentTest, AutoDismissOnTick)
{
    snackbar.show(ToastKind::Info, "Auto dismiss", 50, kT0);
    EXPECT_TRUE(snackbar.isShowing());

    // Before expiry — still showing
    snackbar.tick(kT0 + 49);
    EXPECT_TRUE(snackbar.isShowing());

    // After expiry — auto-dismissed
    snackbar.tick(kT0 + 51);
    EXPECT_FALSE(snackbar.isShowing());
}

// 5. Replace - show "first", then show "second" (same or higher priority), verify replacement
TEST_F(SnackbarComponentTest, ReplacePreviousMessage)
{
    snackbar.show(ToastKind::Info, "first", 5000, kT0);
    EXPECT_TRUE(snackbar.isShowing());
    EXPECT_EQ(snackbar.getMessage(), "first");

    // Info → Info: same priority — should replace
    snackbar.show(ToastKind::Info, "second", 5000, kT0);
    EXPECT_TRUE(snackbar.isShowing());
    EXPECT_EQ(snackbar.getMessage(), "second");
}

// 6. Sizing — the toast is positioned by WindowManager, not internally via
//    parentSizeChanged. Verify the component accepts arbitrary setBounds.
TEST_F(SnackbarComponentTest, SizingCanBeSetExternally)
{
    snackbar.setBounds(100, 100, 280, 50);
    EXPECT_EQ(snackbar.getWidth(), 280);
    EXPECT_EQ(snackbar.getHeight(), 50);
}

} // namespace
