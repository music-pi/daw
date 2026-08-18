#include <gtest/gtest.h>

#include "../src/ui/widget/ChannelSource.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"

#include <algorithm>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

TEST(ChannelSourceTests, GlobalSourceIncludesMasterFirst)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    auto sampleFile = harness.createTemporarySampleFile("s1.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    GlobalChannelSource src(*harness.audio().getEdit());
    auto channels = src.enumerate();
    ASSERT_FALSE(channels.empty());
    EXPECT_EQ(channels.front().kind, ChannelDescriptor::Kind::Master);
    EXPECT_EQ(channels.front().id, "master");
    EXPECT_NE(channels.front().volume, nullptr);
}

TEST(ChannelSourceTests, GlobalSourceIncludesSamplerFolder)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    auto sampleFile = harness.createTemporarySampleFile("s1.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    GlobalChannelSource src(*harness.audio().getEdit());
    auto channels = src.enumerate();

    bool hasFolder = std::any_of(channels.begin(), channels.end(),
        [](const ChannelDescriptor& d) {
            return d.kind == ChannelDescriptor::Kind::Folder && d.canDrillDown;
        });
    EXPECT_TRUE(hasFolder);
}

TEST(ChannelSourceTests, GlobalSourceFolderHasVolumePlugin)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    auto sampleFile = harness.createTemporarySampleFile("s1.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));

    GlobalChannelSource src(*harness.audio().getEdit());
    auto channels = src.enumerate();
    bool found = false;
    for (const auto& d : channels)
    {
        if (d.kind == ChannelDescriptor::Kind::Folder)
        {
            EXPECT_NE(d.folder, nullptr);
            EXPECT_NE(d.volume, nullptr);
            EXPECT_TRUE(d.canDrillDown);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ChannelSourceTests, GroupSourceOnSamplerFolder)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto& pads = harness.pads();

    auto sampleFile = harness.createTemporarySampleFile("s1.wav", 2048, 44100.0);
    ASSERT_TRUE(pads.loadSample(0, sampleFile));
    ASSERT_TRUE(pads.loadSample(1, sampleFile));

    auto* folder = pads.getFolder();
    ASSERT_NE(folder, nullptr);

    GroupChannelSource src(*harness.audio().getEdit(), folder->itemID);
    auto channels = src.enumerate();

    EXPECT_GE(channels.size(), 2u);
    for (const auto& d : channels)
    {
        EXPECT_EQ(d.kind, ChannelDescriptor::Kind::Track);
        EXPECT_FALSE(d.canDrillDown);
        EXPECT_NE(d.track, nullptr);
        EXPECT_FALSE(d.name.startsWith("Sequencer"));
    }
}

TEST(ChannelSourceTests, GroupSourceWithInvalidIdReturnsEmpty)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    GroupChannelSource src(*harness.audio().getEdit(), te::EditItemID{});
    auto channels = src.enumerate();
    EXPECT_TRUE(channels.empty());
}

TEST(ChannelSourceTests, GroupSourceSortsInstrumentSlotsByTheirSavedIndex)
{
    testharness::EngineHarness harness;
    harness.createEmptyEdit();
    auto* edit = harness.audio().getEdit();
    ASSERT_NE(edit, nullptr);
    auto folder = edit->insertNewFolderTrack(
        te::TrackInsertPoint { nullptr, nullptr }, nullptr, false);
    ASSERT_NE(folder, nullptr);

    for (const int slot : { 2, 0, 1 })
    {
        auto track = edit->insertNewAudioTrack(
            te::TrackInsertPoint { folder.get(), nullptr }, nullptr);
        ASSERT_NE(track, nullptr);
        track->state.setProperty("instrumentSlot", slot, nullptr);
        track->setName("Instrument " + juce::String(slot + 1));
    }

    GroupChannelSource source(*edit, folder->itemID);
    const auto channels = source.enumerate();

    ASSERT_EQ(channels.size(), 3u);
    EXPECT_EQ(channels[0].name, "Instrument 1");
    EXPECT_EQ(channels[1].name, "Instrument 2");
    EXPECT_EQ(channels[2].name, "Instrument 3");
}
