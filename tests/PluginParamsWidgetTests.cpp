#include <gtest/gtest.h>
#include "../src/ui/widget/PluginParamsWidget.h"
#include "harness/EngineHarness.h"

namespace te = tracktion::engine;

namespace {
te::Plugin::Ptr addReverb(testharness::EngineHarness& h)
{
    auto& edit = *h.audio().getEdit();
    auto plugin = edit.getPluginCache().createNewPlugin(
        te::ReverbPlugin::xmlTypeName, {});
    auto& list = edit.getMasterPluginList();
    list.insertPlugin(plugin, -1, nullptr);
    return plugin;
}
}

TEST(PluginParamsWidgetTests, NoPluginYieldsNoPages)
{
    PluginParamsWidget w;
    EXPECT_EQ(w.getNumPages(), 0);
}

TEST(PluginParamsWidgetTests, PluginWithNParamsYieldsCeilingOf4PerPage)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto plugin = addReverb(h);

    PluginParamsWidget w;
    w.setPlugin(plugin.get());
    // Reverb has > 4 automatable params; verify at least 1 page, and
    // that the per-page count is <= 4.
    const int n = w.getNumPages();
    EXPECT_GE(n, 1);
    EXPECT_LE(w.getVisibleParamCount(), 4);
}

TEST(PluginParamsWidgetTests, EncoderDeltaAdvancesParameter)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto plugin = addReverb(h);

    PluginParamsWidget w;
    w.setPlugin(plugin.get());
    ASSERT_GE(w.getVisibleParamCount(), 1);

    const auto params = plugin->getAutomatableParameters();
    ASSERT_GE(params.size(), 1);
    const float before = params[0]->getCurrentValue();

    w.onEncoderDelta(/*localIndex*/ 0, /*delta*/ 0.1f);
    const float after = params[0]->getCurrentValue();
    EXPECT_NE(before, after);
}

TEST(PluginParamsWidgetTests, SetPageClampsToValidRange)
{
    testharness::EngineHarness h;
    h.createEmptyEdit();
    auto plugin = addReverb(h);

    PluginParamsWidget w;
    w.setPlugin(plugin.get());
    const int total = w.getNumPages();

    w.setPage(-5);
    EXPECT_EQ(w.getPage(), 0);
    w.setPage(999);
    EXPECT_EQ(w.getPage(), juce::jmax(0, total - 1));
}
