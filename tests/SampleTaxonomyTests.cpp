#include <gtest/gtest.h>

#include "../src/samples/SampleTaxonomy.h"

TEST(SampleTaxonomyTests, LoadsRuntimeJsonAndFallsBackForInvalidDocuments)
{
    const auto parsed = juce::JSON::parse(R"json({
        "schemaVersion": 1,
        "loop": { "minDurationSec": 3.5, "keywords": ["cycle"] },
        "oneShot": { "keywords": ["single"] },
        "instrumentTypes": { "drum": ["beat"] },
        "subTypes": { "kick": ["boom"] },
        "bpmRegex": "(\\d{3})bpm",
        "keyRegex": "([A-G])(maj|min)"
    })json");
    const auto taxonomy = SampleTaxonomy::fromJson(parsed);
    ASSERT_TRUE(taxonomy.has_value());
    EXPECT_DOUBLE_EQ(taxonomy->minimumLoopDurationSeconds(), 3.5);
    EXPECT_EQ(taxonomy->loopKeywords(), std::vector<juce::String> { "cycle" });
    EXPECT_EQ(taxonomy->instrumentKeywords().at("drum").front(), "beat");

    EXPECT_FALSE(SampleTaxonomy::fromJson(juce::JSON::parse("{}")).has_value());
    EXPECT_FALSE(SampleTaxonomy::fromJson(juce::JSON::parse(R"json({
        "schemaVersion": 1,
        "loop": { "minDurationSec": 2.0 },
        "instrumentTypes": { "drum": ["drum"] },
        "subTypes": { "kick": ["kick"] },
        "bpmRegex": "(",
        "keyRegex": "([A-G])(maj|min)"
    })json")).has_value());

    const auto missing = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("missing-taxonomy-" + juce::Uuid().toString());
    bool usedFallback = false;
    const auto fallback = SampleTaxonomy::load(missing, &usedFallback);
    EXPECT_TRUE(usedFallback);
    EXPECT_FALSE(fallback.instrumentKeywords().empty());

    const auto corrupt = missing.getSiblingFile("corrupt-taxonomy-" + juce::Uuid().toString());
    ASSERT_TRUE(corrupt.replaceWithText("{ definitely not json"));
    usedFallback = false;
    const auto corruptFallback = SampleTaxonomy::load(corrupt, &usedFallback);
    EXPECT_TRUE(usedFallback);
    EXPECT_FALSE(corruptFallback.subTypeKeywords().empty());
    EXPECT_TRUE(corrupt.deleteFile());
}
