#include "UiRefresh.h"
#include "IRefreshable.h"
#include "theme/UiTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

void requestUiRefresh(juce::Component& from)
{
    for (auto* c = &from; c != nullptr; c = c->getParentComponent())
    {
        if (auto* host = dynamic_cast<IRefreshable*>(c))
        {
            auto* hostComponent = dynamic_cast<juce::Component*>(host);
            if (hostComponent == nullptr)
            {
                host->requestDisplayRefresh();
                return;
            }

            const auto bounds = hostComponent->getLocalArea(&from, from.getLocalBounds());
            unsigned panelMask = 0u;
            if (bounds.getX() < UiTheme::kPanelWidth && bounds.getRight() > 0)
                panelMask |= 1u;
            if (bounds.getX() < UiTheme::kTotalWidth
                && bounds.getRight() > UiTheme::kPanelWidth)
                panelMask |= 2u;

            host->requestDisplayRefresh(panelMask != 0u ? panelMask : 3u);
            return;
        }
    }
}
