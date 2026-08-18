#include <gtest/gtest.h>
#include "../src/engine/PluginCatalog.h"
#include "harness/EngineHarness.h"

TEST(PluginCatalogTests, InitialStateIsNotScanning)
{
    testharness::EngineHarness harness;
    PluginCatalog cat (harness.audio().getEngine());
    EXPECT_FALSE(cat.isScanning());
}

TEST(PluginCatalogTests, BuiltInInstrumentIsAvailableBeforeFirstScan)
{
    testharness::EngineHarness harness;
    PluginCatalog cat (harness.audio().getEngine());
    const auto expected = tracktion::engine::PluginManager::
        createBuiltInPluginDescription<tracktion::engine::FourOscPlugin>(true);

    const auto instruments = cat.getInstruments();
    ASSERT_EQ(instruments.size(), 1);
    EXPECT_EQ(instruments[0].name, expected.name);
    EXPECT_EQ(instruments[0].fileOrIdentifier, expected.fileOrIdentifier);
    EXPECT_EQ(cat.getEffects().size(), 0);
    EXPECT_FALSE(cat.hasDiscoveredPlugins(true));
}

TEST(PluginCatalogTests, FilterSortsInstrumentsByManufacturerAndName)
{
    testharness::EngineHarness harness;
    PluginCatalog cat (harness.audio().getEngine());

    auto& known = harness.audio().getEngine().getPluginManager().knownPluginList;

    // fileOrIdentifier must be unique — KnownPluginList::addType treats
    // descriptors with identical (fileOrIdentifier, deprecatedUid, uniqueId)
    // as duplicates and rejects them.
    juce::PluginDescription a; a.name = "Zeta";  a.manufacturerName = "A"; a.isInstrument = true;
    a.fileOrIdentifier = "fake://a"; a.uniqueId = 1;
    juce::PluginDescription b; b.name = "Alpha"; b.manufacturerName = "B"; b.isInstrument = true;
    b.fileOrIdentifier = "fake://b"; b.uniqueId = 2;
    juce::PluginDescription c; c.name = "Beta";  c.manufacturerName = "A"; c.isInstrument = false;
    c.fileOrIdentifier = "fake://c"; c.uniqueId = 3;

    ASSERT_TRUE (known.addType (a));
    ASSERT_TRUE (known.addType (b));
    ASSERT_TRUE (known.addType (c));

    auto instruments = cat.getInstruments();
    ASSERT_EQ(instruments.size(), 3);
    EXPECT_TRUE(cat.hasDiscoveredPlugins(true));

    int zetaIndex = -1;
    int alphaIndex = -1;
    for (int i = 0; i < instruments.size(); ++i)
    {
        if (instruments[i].name == "Zeta")
            zetaIndex = i;
        if (instruments[i].name == "Alpha")
            alphaIndex = i;
    }
    EXPECT_GE(zetaIndex, 0);
    EXPECT_GE(alphaIndex, 0);
    EXPECT_LT(zetaIndex, alphaIndex);

    auto effects = cat.getEffects();
    ASSERT_EQ(effects.size(), 1);
    EXPECT_EQ(effects[0].name, "Beta");
    EXPECT_TRUE(cat.hasDiscoveredPlugins(false));
}
