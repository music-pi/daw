#pragma once

namespace juce { class Component; }

/** Walks up the Component tree to find the nearest IRefreshable ancestor
    (typically UiHost) and requests a refresh for the panel(s) occupied by
    the source component. Components crossing the panel seam mark both.
    Safe to call only from the message thread. */
void requestUiRefresh(juce::Component& from);
