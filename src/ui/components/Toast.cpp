#include "Toast.h"

#include "../UiRefresh.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace
{

juce::Colour accentForKind(ToastKind kind)
{
    switch (kind)
    {
        case ToastKind::Info:    return UiTheme::ToastColors::kInfo;
        case ToastKind::Success: return UiTheme::ToastColors::kSuccess;
        case ToastKind::Warning: return UiTheme::ToastColors::kWarning;
        case ToastKind::Error:   return UiTheme::ToastColors::kError;
        case ToastKind::Loading: return UiTheme::ToastColors::kLoading;
    }
    return UiTheme::ToastColors::kInfo;
}

const char* iconForKind(ToastKind kind)
{
    switch (kind)
    {
        case ToastKind::Info:    return "\u24d8"; // ⓘ
        case ToastKind::Success: return "\u2713"; // ✓
        case ToastKind::Warning: return "\u26a0"; // ⚠
        case ToastKind::Error:   return "\u2715"; // ✕
        case ToastKind::Loading: return nullptr;  // animated dots, no glyph
    }
    return nullptr;
}

} // namespace

// ── ToastComponent ────────────────────────────────────────────────────────────

ToastComponent::ToastComponent()
{
    setInterceptsMouseClicks(false, false);
    setVisible(false);
}

uint64_t ToastComponent::show(ToastKind kind, const juce::String& msg, int durationMs, int64_t nowMs)
{
    if (nowMs < 0)
        nowMs = static_cast<int64_t>(juce::Time::getMillisecondCounter());

    // Priority check: only replace if new priority >= active priority OR active toast expired
    if (isVisible())
    {
        const bool expired = (expiryMs_ > 0 && nowMs >= expiryMs_);
        if (!expired && toastPriority(kind) < toastPriority(kind_))
            return 0; // dropped silently
    }

    kind_    = kind;
    message_ = msg;

    int effectiveDuration = (durationMs > 0) ? durationMs : defaultDurationMs(kind);
    expiryMs_ = (effectiveDuration > 0) ? (nowMs + effectiveDuration) : 0;

    animPhase_  = 0;
    lastAnimMs_ = nowMs;

    ++handleId_;

    setVisible(true);
    repaint();
    requestUiRefresh(*this);

    return handleId_;
}

void ToastComponent::dismiss()
{
    const bool wasVisible = isVisible();
    setVisible(false);
    ++handleId_; // invalidate any outstanding handles
    if (wasVisible)
    {
        // Clear the toast's pixel footprint — ToastComponent is not a Widget
        // so repaint() doesn't auto-flip the UiHost dirty flag via the Widget
        // shadow; we have to pair explicitly here.
        repaint();
        requestUiRefresh(*this);
    }
}

void ToastComponent::tick(int64_t nowMs)
{
    if (!isVisible())
        return;

    // Auto-dismiss when expired (Loading has expiryMs_ == 0, never expires)
    if (expiryMs_ > 0 && nowMs >= expiryMs_)
    {
        dismiss();
        return;
    }

    // Advance loading animation
    if (kind_ == ToastKind::Loading)
    {
        int newPhase = static_cast<int>((nowMs / kAnimIntervalMs) % 3);
        if (newPhase != animPhase_)
        {
            animPhase_ = newPhase;
            repaint();
            requestUiRefresh(*this);
        }
    }
}

void ToastComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(UiTheme::kSnackbarBackground);
    g.fillRoundedRectangle(bounds, 4.0f);

    const juce::Colour accent = accentForKind(kind_);
    const int stripe = UiTheme::ToastColors::kStripeWidth;
    const int gutter = UiTheme::ToastColors::kIconGutter;

    // Accent stripe — full height, flush left
    g.setColour(accent);
    g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(stripe), bounds.getHeight()));

    if (kind_ == ToastKind::Loading)
    {
        // Animated three-dot pulse
        constexpr float kDotRadius = 3.0f;
        constexpr float kDotSpacing = 6.0f;
        constexpr int kNumDots = 3;

        const float totalWidth = kNumDots * kDotRadius * 2.0f + (kNumDots - 1) * kDotSpacing;
        const float startX = static_cast<float>(stripe) + (static_cast<float>(gutter) - totalWidth) * 0.5f;
        const float centreY = bounds.getHeight() * 0.5f;

        for (int i = 0; i < kNumDots; ++i)
        {
            const float cx = startX + i * (kDotRadius * 2.0f + kDotSpacing) + kDotRadius;
            // The "active" dot in the current phase pulses bright; others dim
            const float alpha = (i == animPhase_) ? 1.0f : 0.35f;
            g.setColour(accent.withAlpha(alpha));
            g.fillEllipse(cx - kDotRadius, centreY - kDotRadius, kDotRadius * 2.0f, kDotRadius * 2.0f);
        }
    }
    else
    {
        // Static icon glyph
        if (const char* icon = iconForKind(kind_))
        {
            g.setColour(accent);
            g.setFont(juce::Font(juce::FontOptions().withHeight(16.0f)));
            g.drawText(juce::String::fromUTF8(icon),
                       stripe, 0, gutter - stripe, static_cast<int>(bounds.getHeight()),
                       juce::Justification::centred, false);
        }
    }

    // Message text
    const int textX = stripe + gutter;
    const int textW = static_cast<int>(bounds.getWidth()) - textX - UiTheme::kPadding;
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions("Inter", UiTheme::Fonts::kSnackbar, juce::Font::bold)));
    g.drawText(message_,
               textX, 0, textW, static_cast<int>(bounds.getHeight()),
               juce::Justification::centredLeft, true);
}

// ── ToastHandle ───────────────────────────────────────────────────────────────

bool ToastHandle::isOwned() const
{
    if (id_ == 0)
        return false;
    auto* t = toast_.get();
    return (t != nullptr) && (t->getCurrentHandleId() == id_);
}

void ToastHandle::resolve(ToastKind finalKind, const juce::String& msg)
{
    if (!isOwned())
        return;
    auto* t = toast_.get();
    if (t != nullptr)
    {
        // Dismiss the loading toast first so the priority check is bypassed:
        // after dismiss() the component is invisible, so the next show()
        // always succeeds regardless of priority.
        t->dismiss();
        t->show(finalKind, msg);
    }
    id_ = 0;
}

void ToastHandle::dismiss()
{
    if (!isOwned())
        return;
    auto* t = toast_.get();
    if (t != nullptr)
        t->dismiss();
    id_ = 0;
}
