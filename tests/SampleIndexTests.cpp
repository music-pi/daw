#include <gtest/gtest.h>

#include "../src/samples/SampleIndex.h"

namespace
{
SampleIndexEntry entryFor(const juce::String& path,
                          SampleCategory category,
                          SampleInstrumentType instrument,
                          juce::String subType = {})
{
    SampleIndexEntry entry;
    entry.file = juce::File(path);
    entry.modificationTimeMs = 123;
    entry.size = 456;
    entry.durationSeconds = 1.25;
    entry.tags.category = category;
    entry.tags.instrumentType = instrument;
    entry.tags.subType = std::move(subType);
    entry.tags.source = SampleTagSource::Heuristic;
    entry.tags.confidence = 0.9;
    return entry;
}
}

TEST(SampleIndexTests, SynthesizesNavigableCategoryTreeWithRealFileLeaves)
{
    SampleIndex index;
    auto kick = entryFor("/samples/Kit One/Kick.wav", SampleCategory::OneShot,
                         SampleInstrumentType::Drum, "kick");
    kick.tags.kit = "Kit One";
    index.set(kick);
    auto loop = entryFor("/samples/Bass/bass_loop_125bpm_Cmin.wav", SampleCategory::Loop,
                         SampleInstrumentType::Bass);
    loop.tags.bpm = 125;
    loop.tags.key = "C min";
    index.set(loop);

    const auto tree = index.buildCategoryTree();
    ASSERT_EQ(tree.size(), 2u);

    std::function<bool(const std::vector<SampleCategoryNode>&, const juce::String&)> containsFile;
    containsFile = [&](const auto& nodes, const juce::String& path)
    {
        for (const auto& node : nodes)
        {
            if (node.isFile && node.file.getFullPathName() == path)
                return true;
            if (containsFile(node.children, path))
                return true;
        }
        return false;
    };
    EXPECT_TRUE(containsFile(tree, kick.file.getFullPathName()));
    EXPECT_TRUE(containsFile(tree, loop.file.getFullPathName()));
}

TEST(SampleIndexTests, JsonRoundTripPreservesMetadataAndRejectsAnotherRoot)
{
    const juce::File root("/samples");
    SampleIndex index;
    auto entry = entryFor("/samples/Kick.wav", SampleCategory::OneShot,
                          SampleInstrumentType::Drum, "kick");
    entry.tags.bpm = 120;
    index.set(entry);

    const auto json = index.toJson(root);
    const auto restored = SampleIndex::fromJson(json, root);
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->size(), 1u);
    const auto* restoredEntry = restored->find(entry.file);
    ASSERT_NE(restoredEntry, nullptr);
    EXPECT_EQ(restoredEntry->tags, entry.tags);
    EXPECT_EQ(restoredEntry->durationSeconds, entry.durationSeconds);

    EXPECT_FALSE(SampleIndex::fromJson(json, juce::File("/different")).has_value());
}
