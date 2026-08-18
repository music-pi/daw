#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../theme/UiTheme.h"

// ── Toast kinds ───────────────────────────────────────────────────────────────

enum class ToastKind { Info, Success, Warning, Error, Loading };

// ── Priority helpers ──────────────────────────────────────────────────────────

inline int toastPriority(ToastKind kind)
{
    switch (kind)
    {
        case ToastKind::Info:    return 0;
        case ToastKind::Success: return 1;
        case ToastKind::Loading: return 2;
        case ToastKind::Warning: return 3;
        case ToastKind::Error:   return 4;
    }
    return 0;
}

// ── Default durations (ms) ────────────────────────────────────────────────────

inline int defaultDurationMs(ToastKind kind)
{
    switch (kind)
    {
        case ToastKind::Info:    return 2500;
        case ToastKind::Success: return 2000;
        case ToastKind::Warning: return 4000;
        case ToastKind::Error:   return 5000;
        case ToastKind::Loading: return 0; // indeterminate — never auto-dismisses
    }
    return 2500;
}

// ── ToastComponent ────────────────────────────────────────────────────────────

/**
 * ToastComponent — typed replacement for SnackbarComponent.
 *
 * Owned by WindowManager (one per panel). All timing is driven externally
 * via `tick(nowMs)` — no internal juce::Timer — so the component is
 * unit-testable without a running message loop.
 *
 * Priority model: a new `show()` replaces the active toast only if
 *   newPriority >= activePriority  OR  the active toast has expired.
 *
 * Loading toasts are indeterminate (never expire on their own); they are
 * dismissed by calling `dismiss()` or by an equal/higher-priority `show()`.
 */
class ToastComponent : public juce::Component
{
public:
    ToastComponent();

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Show a toast. If a higher-priority unexpired toast is already visible,
     * the new toast is silently dropped.
     * Pass durationMs = 0 to use the kind's default duration.
     *
     * @return the handle id issued for this show, or 0 if the toast was
     *   silently dropped due to a higher-priority active toast. Callers that
     *   care about ownership should compare the returned id against
     *   getCurrentHandleId() — they only match if this show actually took.
     *   Id 0 is reserved as "never issued"; real ids start at 1.
     *
     * @param nowMs optional "current time" override for deterministic tests.
     *   Pass -1 (default) to use juce::Time::getMillisecondCounter().
     */
    uint64_t show(ToastKind kind, const juce::String& msg, int durationMs = 0,
                  int64_t nowMs = -1);

    /** Dismiss the current toast immediately. */
    void dismiss();

    /** Returns true if a toast is currently visible. */
    bool isShowing() const { return isVisible(); }

    /** Current kind (only meaningful when isShowing()). */
    ToastKind getKind() const { return kind_; }

    /** Current message (only meaningful when isShowing()). */
    const juce::String& getMessage() const { return message_; }

    /**
     * Returns the handle id issued by the most recent successful show().
     * A ToastHandle holds the id it was issued; if they still match the
     * handle "owns" the toast and dismiss/resolve ops take effect.
     */
    uint64_t getCurrentHandleId() const { return handleId_; }

    /**
     * Advance animation phase and handle expiry. WindowManager calls this
     * once per UiHost tick.
     *
     * @param nowMs millisecond timestamp from juce::Time::getMillisecondCounter().
     *   This is NOT a Unix epoch — it's uptime milliseconds since process start.
     *   The component compares this against expiryMs_, which was computed by an
     *   earlier show() using the same clock, so the two must be consistent. Pass
     *   an explicit value in tests for deterministic animation/expiry.
     */
    void tick(int64_t nowMs);

    // ── juce::Component ──────────────────────────────────────────────────────

    void paint(juce::Graphics& g) override;

    JUCE_DECLARE_WEAK_REFERENCEABLE(ToastComponent)

private:
    // Current state
    ToastKind kind_    = ToastKind::Info;
    juce::String message_;
    int64_t expiryMs_  = 0;  // 0 = indeterminate (Loading)
    int animPhase_     = 0;  // 0-2 for loading animation cycle position
    int64_t lastAnimMs_= 0;
    // uint64 avoids wraparound in long-running sessions — an int32 at
    // one show/sec would still take decades to overflow, but uint64 makes
    // the ownership-check invariant unconditionally sound.
    uint64_t handleId_ = 0;  // 0 = never issued; incremented on every new show()

    static constexpr int kAnimIntervalMs = 200; // per-dot offset

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToastComponent)
};

// ── ToastHandle ───────────────────────────────────────────────────────────────

/**
 * RAII handle returned by Widget::showLoadingToast().
 *
 * Dropping the handle (or calling dismiss()) automatically dismisses the
 * loading toast if this handle still owns it. Calling resolve() replaces it
 * with a final-kind toast.
 *
 * "Owns" = the handle's id still matches the component's current handleId_.
 * The component increments its id on every new show() (including the one from
 * a resolve()), so any subsequent show from another caller invalidates older
 * handles and their destructors become no-ops. This is the non-interference
 * guarantee: dropping a stale handle never clobbers a newer toast.
 */
class ToastHandle
{
public:
    /** Null handle — no-op on drop. */
    ToastHandle() = default;

    /** Construct with a weak reference to the toast and the handle id. */
    ToastHandle(juce::WeakReference<ToastComponent> toast, uint64_t id)
        : toast_(std::move(toast)), id_(id) {}

    ToastHandle(const ToastHandle&) = delete;
    ToastHandle& operator=(const ToastHandle&) = delete;

    ToastHandle(ToastHandle&&) noexcept = default;
    ToastHandle& operator=(ToastHandle&&) noexcept = default;

    ~ToastHandle() { dismiss(); }

    /**
     * Replace the loading toast with a final Success/Warning/Error/Info toast.
     * If the handle no longer owns the toast (superseded), this is a no-op.
     *
     * Note: resolve() deliberately bypasses the priority gate — the owner of a
     * Loading toast (priority 2) must be able to downgrade it to Info or
     * Success (priorities 0-1). This is safe because only the handle owner
     * can call resolve(), and the ownership check (id match) ensures the
     * original caller hasn't already been superseded by something higher.
     */
    void resolve(ToastKind finalKind, const juce::String& msg);

    /**
     * Dismiss the loading toast without a replacement.
     * No-op if no longer owned or already dismissed.
     */
    void dismiss();

    /** Returns true if this handle still owns its toast component. */
    bool isOwned() const;

private:
    juce::WeakReference<ToastComponent> toast_;
    uint64_t id_ = 0;  // 0 = null handle / never owned
};
