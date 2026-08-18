#pragma once

/** Minimal interface for components that own a hardware display push loop.
    UiHost implements this so UiRefresh can mark the display dirty without
    pulling in the full UiHost dependency. */
class IRefreshable
{
public:
    virtual ~IRefreshable() = default;
    virtual void requestDisplayRefresh() noexcept = 0;

    /** Mark selected panels dirty (bit 0 = left, bit 1 = right).
        Hosts that do not implement panel tracking retain the safe all-panels
        behaviour through this default. */
    virtual void requestDisplayRefresh(unsigned /*panelMask*/) noexcept
    {
        requestDisplayRefresh();
    }
};
