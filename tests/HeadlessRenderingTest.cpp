#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "harness/JuceHarness.h"
#include "../src/ui/components/ListComponent.h"

namespace
{

// Helper to check if image has any non-transparent pixels
bool hasVisibleContent(const juce::Image& img)
{
    if (img.isNull() || img.getWidth() == 0 || img.getHeight() == 0)
        return false;

    for (int y = 0; y < img.getHeight(); ++y)
    {
        for (int x = 0; x < img.getWidth(); ++x)
        {
            if (img.getPixelAt(x, y).getAlpha() > 0)
                return true;
        }
    }
    return false;
}

int countDifferentPixels(const juce::Image& first, const juce::Image& second)
{
    if (first.getWidth() != second.getWidth() || first.getHeight() != second.getHeight())
        return -1;

    int differences = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            differences += first.getPixelAt(x, y) != second.getPixelAt(x, y);
    return differences;
}

// Render component using headless-safe method (paintEntireComponent)
juce::Image renderHeadless(juce::Component& component)
{
    const auto bounds = component.getLocalBounds();
    if (bounds.isEmpty())
        return {};

    juce::Image img(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
    juce::Graphics g(img);
    component.paintEntireComponent(g, true);
    return img;
}

} // namespace

class HeadlessRenderingTest : public ::testing::Test
{
protected:
    testharness::JuceFrameworkContext juceContext;
};

TEST_F(HeadlessRenderingTest, ListComponentRendersNonBlank)
{
    ListComponent list;
    list.setBounds(0, 0, 480, 200);

    // Add some items so there's content to render
    std::vector<ListComponent::Item> items;
    items.push_back({ "item1", "Test Item 1", false, false, true, false, "", {} });
    items.push_back({ "item2", "Test Item 2", false, false, true, false, "", {} });
    items.push_back({ "item3", "Test Item 3", false, false, true, false, "", {} });
    list.setItems(std::move(items));

    auto img = renderHeadless(list);
    ListComponent emptyList;
    emptyList.setBounds(0, 0, 480, 200);
    auto emptyImg = renderHeadless(emptyList);

    ASSERT_FALSE(img.isNull()) << "Rendered image should not be null";
    EXPECT_TRUE(hasVisibleContent(img)) << "ListComponent should render visible content";
    EXPECT_GT(countDifferentPixels(img, emptyImg), 0)
        << "List items should change the rendered output";
}

TEST_F(HeadlessRenderingTest, EmptyListRendersEmptyMessage)
{
    ListComponent list;
    list.setBounds(0, 0, 480, 200);
    list.setEmptyMessage("No items");
    auto img = renderHeadless(list);

    ASSERT_FALSE(img.isNull()) << "Rendered image should not be null";
    EXPECT_TRUE(hasVisibleContent(img)) << "Empty list should render empty message";

    list.setEmptyMessage("Different message");
    auto changedImg = renderHeadless(list);
    EXPECT_GT(countDifferentPixels(img, changedImg), 0)
        << "Changing the empty message should change the rendered output";
}

TEST_F(HeadlessRenderingTest, ListWithSelectionRendersHighlight)
{
    ListComponent list;
    list.setBounds(0, 0, 480, 200);

    std::vector<ListComponent::Item> items;
    items.push_back({ "item1", "Test Item 1", false, false, true, false, "", {} });
    items.push_back({ "item2", "Test Item 2", false, false, true, false, "", {} });
    list.setItems(std::move(items));

    auto unselectedImg = renderHeadless(list);
    list.setSelectedId("item1");
    auto img = renderHeadless(list);

    ASSERT_FALSE(img.isNull()) << "Rendered image should not be null";
    EXPECT_TRUE(hasVisibleContent(img)) << "List with selection should render visible content";
    EXPECT_GT(countDifferentPixels(unselectedImg, img), 0)
        << "Selection should change the rendered output";
}
