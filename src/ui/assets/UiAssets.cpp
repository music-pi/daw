#include "UiAssets.h"

#include <juce_graphics/juce_graphics.h>

#include <UiBinaryData.h>

namespace UiAssets
{
juce::Image getDefaultScreenBackground()
{
    static const juce::Image background = []
    {
        return juce::ImageFileFormat::loadFrom(UiBinaryData::bgdefault_png,
                                               UiBinaryData::bgdefault_pngSize);
    }();

    return background;
}
} // namespace UiAssets
