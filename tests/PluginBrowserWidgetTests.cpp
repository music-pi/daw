#include <gtest/gtest.h>
#include "../src/ui/widget/PluginBrowserWidget.h"
#include "../src/engine/PluginCatalog.h"
#include "harness/EngineHarness.h"

namespace {

void addFakePlugin(juce::KnownPluginList& list, const juce::String& name,
                   const juce::String& manu, bool instrument,
                   const juce::String& unique)
{
    juce::PluginDescription d;
    d.name = name;
    d.manufacturerName = manu;
    d.isInstrument = instrument;
    d.fileOrIdentifier = unique;
    d.uniqueId = unique.hashCode();
    list.addType(d);
}

} // namespace

TEST(PluginBrowserWidgetTests, EmptyCatalogYieldsZeroItems)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    PluginCatalog cat(harness.audio().getEngine());
    PluginBrowserWidget w(cat, PluginBrowserWidget::Filter::Effects);
    EXPECT_EQ(w.getItemCount(), 0);
    EXPECT_EQ(w.getSelectedIndex(), -1);
}

TEST(PluginBrowserWidgetTests, InstrumentsFilterHidesEffects)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& known = harness.audio().getEngine().getPluginManager().knownPluginList;
    addFakePlugin(known, "Synth", "M", /*instr*/ true,  "synth-id");
    addFakePlugin(known, "Reverb","M", /*instr*/ false, "reverb-id");

    PluginCatalog cat(harness.audio().getEngine());
    PluginBrowserWidget w(cat, PluginBrowserWidget::Filter::Instruments);
    EXPECT_EQ(w.getItemCount(), 2); // scanned synth + built-in FourOsc
}

TEST(PluginBrowserWidgetTests, BuiltInInstrumentCanBePickedWithoutScanning)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    PluginCatalog cat(harness.audio().getEngine());
    PluginBrowserWidget w(cat, PluginBrowserWidget::Filter::Instruments);
    ASSERT_EQ(w.getItemCount(), 1);

    struct Listener : PluginBrowserWidget::Listener
    {
        juce::PluginDescription selected;
        void pluginSelected(const juce::PluginDescription& description) override
        {
            selected = description;
        }
    } listener;

    w.setListener(&listener);
    w.moveSelection(+1);
    w.confirmSelection();

    const auto expected = tracktion::engine::PluginManager::
        createBuiltInPluginDescription<tracktion::engine::FourOscPlugin>(true);
    EXPECT_EQ(listener.selected.fileOrIdentifier, expected.fileOrIdentifier);
}

TEST(PluginBrowserWidgetTests, MoveSelectionClampsToRange)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& known = harness.audio().getEngine().getPluginManager().knownPluginList;
    addFakePlugin(known, "A", "M", false, "a-id");
    addFakePlugin(known, "B", "M", false, "b-id");
    addFakePlugin(known, "C", "M", false, "c-id");

    PluginCatalog cat(harness.audio().getEngine());
    PluginBrowserWidget w(cat, PluginBrowserWidget::Filter::Effects);
    w.refreshItems();
    ASSERT_EQ(w.getItemCount(), 3);

    w.moveSelection(+1);
    EXPECT_EQ(w.getSelectedIndex(), 0);
    w.moveSelection(+1);
    EXPECT_EQ(w.getSelectedIndex(), 1);
    w.moveSelection(-1);
    EXPECT_EQ(w.getSelectedIndex(), 0);
    w.moveSelection(+100);
    EXPECT_EQ(w.getSelectedIndex(), 2);
    w.moveSelection(-100);
    EXPECT_EQ(w.getSelectedIndex(), 0);
}

TEST(PluginBrowserWidgetTests, ConfirmSelectionInvokesListener)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& known = harness.audio().getEngine().getPluginManager().knownPluginList;
    addFakePlugin(known, "Picked", "M", false, "picked-id");

    PluginCatalog cat(harness.audio().getEngine());
    PluginBrowserWidget w(cat, PluginBrowserWidget::Filter::Effects);
    w.refreshItems();
    w.moveSelection(+1);

    struct Listener : PluginBrowserWidget::Listener {
        juce::PluginDescription last;
        int cancelCount = 0;
        void pluginSelected(const juce::PluginDescription& d) override { last = d; }
        void browserCancelled() override { ++cancelCount; }
    } listener;

    w.setListener(&listener);
    w.confirmSelection();
    EXPECT_EQ(listener.last.name, "Picked");
    EXPECT_EQ(listener.cancelCount, 0);

    w.cancel();
    EXPECT_EQ(listener.cancelCount, 1);
}
