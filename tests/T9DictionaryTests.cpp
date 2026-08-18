#include <gtest/gtest.h>
#include "../src/engine/T9Dictionary.h"
#include <juce_data_structures/juce_data_structures.h>

TEST(T9DictionaryTests, SeededValuesSurfaceAsCompletions)
{
    juce::ValueTree root("t9");
    T9Dictionary dict(root);
    dict.seed("proj", juce::StringArray { "Alpha Session", "Beta Mix", "Charlie Take" });

    auto hits = dict.completionsFor("proj", "Al", 5);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], "Alpha Session");
}

TEST(T9DictionaryTests, RecordAddsAndBoostsEntries)
{
    juce::ValueTree root("t9");
    T9Dictionary dict(root);
    dict.record("proj", "Demo");
    dict.record("proj", "Demo");
    dict.record("proj", "Different");

    auto hits = dict.completionsFor("proj", "", 5);
    ASSERT_GE(hits.size(), 2u);
    EXPECT_EQ(hits[0], "Demo");      // higher use count ranks first
}

TEST(T9DictionaryTests, CompletionsRespectScope)
{
    juce::ValueTree root("t9");
    T9Dictionary dict(root);
    dict.seed("a", juce::StringArray { "apple" });
    dict.seed("b", juce::StringArray { "banana" });

    auto hitsA = dict.completionsFor("a", "", 5);
    ASSERT_EQ(hitsA.size(), 1u);
    EXPECT_EQ(hitsA[0], "apple");

    auto hitsB = dict.completionsFor("b", "", 5);
    ASSERT_EQ(hitsB.size(), 1u);
    EXPECT_EQ(hitsB[0], "banana");
}

TEST(T9DictionaryTests, PrefixMatchIsCaseInsensitive)
{
    juce::ValueTree root("t9");
    T9Dictionary dict(root);
    dict.seed("s", juce::StringArray { "MyProject", "mysession", "MYBEAT" });
    auto hits = dict.completionsFor("s", "my", 10);
    EXPECT_EQ(hits.size(), 3u);
}

TEST(T9DictionaryTests, LimitParameterTruncates)
{
    juce::ValueTree root("t9");
    T9Dictionary dict(root);
    for (int i = 0; i < 10; ++i)
        dict.record("x", "item" + juce::String(i));
    auto hits = dict.completionsFor("x", "item", 3);
    EXPECT_EQ(hits.size(), 3u);
}

TEST(T9DictionaryTests, SeedIsIdempotent)
{
    juce::ValueTree root("t9");
    T9Dictionary dict(root);
    dict.seed("proj", juce::StringArray { "Alpha", "Beta" });
    dict.seed("proj", juce::StringArray { "Alpha", "Beta", "Gamma" });
    EXPECT_EQ(dict.entryCount("proj"), 3);
}

TEST(T9DictionaryTests, RecentEntryOutranksOldEntryOfSameFrequency)
{
    juce::ValueTree root("t9");

    // Add an old entry manually (simulate 120 days ago).
    const auto nowMs = juce::Time::currentTimeMillis();
    const juce::int64 oldMs = nowMs - juce::int64(120) * 24 * 60 * 60 * 1000;

    juce::ValueTree oldEntry("entry");
    oldEntry.setProperty("scope", "x", nullptr);
    oldEntry.setProperty("text",  "ancient", nullptr);
    oldEntry.setProperty("uses",  1, nullptr);
    oldEntry.setProperty("lastUsedMs", oldMs, nullptr);
    root.appendChild(oldEntry, nullptr);

    T9Dictionary dict(root);
    dict.record("x", "fresh");           // uses=1, recent

    auto hits = dict.completionsFor("x", "", 10);
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], "fresh");         // decay pushes "ancient" below "fresh"
}

TEST(T9DictionaryTests, PersistenceRoundTripViaValueTree)
{
    juce::ValueTree root("t9");
    {
        T9Dictionary dict(root);
        dict.record("p", "Kept");
        dict.record("p", "Kept");
    }
    // Same ValueTree, fresh dictionary instance:
    T9Dictionary revived(root);
    auto hits = revived.completionsFor("p", "", 5);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], "Kept");
}
