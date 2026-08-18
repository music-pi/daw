#include <gtest/gtest.h>

#include "../src/samples/SampleClassifier.h"

TEST(SampleClassifierTests, UsesNamesForDrumSubtypeKitBpmAndKey)
{
    SampleClassifier classifier(SampleTaxonomy::defaults());
    const juce::File file("/library/Neon Kit/Kick_one-shot_124bpm_Cmin.wav");
    int durationReads = 0;
    const auto tags = classifier.classify(file, [&](const juce::File&) {
        ++durationReads;
        return std::optional<double> { 8.0 };
    });

    EXPECT_EQ(tags.category, SampleCategory::OneShot);
    EXPECT_EQ(tags.instrumentType, SampleInstrumentType::Drum);
    EXPECT_EQ(tags.subType, "kick");
    EXPECT_EQ(tags.kit, "Neon Kit");
    ASSERT_TRUE(tags.bpm.has_value());
    EXPECT_EQ(*tags.bpm, 124);
    EXPECT_EQ(tags.key, "C min");
    EXPECT_EQ(tags.source, SampleTagSource::Heuristic);
    EXPECT_EQ(durationReads, 0);
}

TEST(SampleClassifierTests, UsesDurationOnlyWhenNamesDoNotSettleCategory)
{
    SampleClassifier classifier(SampleTaxonomy::defaults());
    const auto longTags = classifier.classify(juce::File("/library/texture.wav"),
        [](const juce::File&) { return std::optional<double> { 2.5 }; });
    EXPECT_EQ(longTags.category, SampleCategory::Loop);
    EXPECT_EQ(longTags.source, SampleTagSource::Analysis);

    const auto shortTags = classifier.classify(juce::File("/library/texture.wav"),
        [](const juce::File&) { return std::optional<double> { 0.2 }; });
    EXPECT_EQ(shortTags.category, SampleCategory::OneShot);

    const auto unreadable = classifier.classify(juce::File("/library/broken.wav"),
        [](const juce::File&) { return std::optional<double> {}; });
    EXPECT_EQ(unreadable.category, SampleCategory::Unknown);
    EXPECT_EQ(unreadable.instrumentType, SampleInstrumentType::Unknown);
}

TEST(SampleClassifierTests, FilenameHeuristicWinsOverDuration)
{
    SampleClassifier classifier(SampleTaxonomy::defaults());
    int reads = 0;
    const auto tags = classifier.classify(juce::File("/library/ambient_loop.wav"),
        [&](const juce::File&) {
            ++reads;
            return std::optional<double> { 0.1 };
        });
    EXPECT_EQ(tags.category, SampleCategory::Loop);
    EXPECT_EQ(tags.source, SampleTagSource::Heuristic);
    EXPECT_EQ(reads, 0);
}

TEST(SampleClassifierTests, RecognizesConfiguredDrumSubtypes)
{
    SampleClassifier classifier(SampleTaxonomy::defaults());
    struct Case { const char* name; const char* expected; };
    const std::vector<Case> cases {
        { "Snare_hit.wav", "snare" },
        { "Closed_HH_one-shot.wav", "hat" },
        { "Hand_Clap_hit.wav", "clap" },
        { "Ride_Cymbal_hit.wav", "cymbal" }
    };
    for (const auto& item : cases)
    {
        const auto tags = classifier.classify(juce::File("/library/Drums/" + juce::String(item.name)),
            [](const juce::File&) { return std::optional<double> { 0.2 }; });
        EXPECT_EQ(tags.instrumentType, SampleInstrumentType::Drum) << item.name;
        EXPECT_EQ(tags.subType, item.expected) << item.name;
        EXPECT_EQ(tags.category, SampleCategory::OneShot) << item.name;
    }
}
