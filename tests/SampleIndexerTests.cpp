#include <gtest/gtest.h>

#include "../src/samples/SampleIndexer.h"

namespace
{
juce::File writeWav(const juce::File& directory,
                    const juce::String& name,
                    double durationSeconds)
{
    const auto file = directory.getChildFile(name);
    file.deleteFile();
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    if (stream == nullptr)
        return {};
    constexpr double sampleRate = 8000.0;
    juce::AudioBuffer<float> buffer(1, static_cast<int>(sampleRate * durationSeconds));
    buffer.clear();
    juce::WavAudioFormat format;
    auto options = juce::AudioFormatWriterOptions()
                       .withSampleRate(sampleRate)
                       .withNumChannels(1)
                       .withBitsPerSample(16);
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream, options));
    if (writer == nullptr)
        return {};
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();
    return file;
}

bool waitForIdle(SampleIndexer& indexer)
{
    for (int attempt = 0; attempt < 500; ++attempt)
    {
        if (!indexer.isScanning())
            return true;
        juce::Thread::sleep(10);
    }
    return false;
}
}

TEST(SampleIndexerTests, IncrementallyAddsChangesAndRemovesFiles)
{
    const auto base = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("sample-indexer-" + juce::Uuid().toString());
    ASSERT_TRUE(base.createDirectory().wasOk());
    const auto root = base.getChildFile("samples");
    ASSERT_TRUE(root.createDirectory().wasOk());
    const auto indexFile = base.getChildFile("samples-index.json");
    const auto taxonomyFile = base.getChildFile("sample-taxonomy.json");
    const auto shippedTaxonomy = SampleTaxonomy::findDefaultFile();
    ASSERT_TRUE(shippedTaxonomy.existsAsFile());
    ASSERT_TRUE(taxonomyFile.replaceWithText(shippedTaxonomy.loadFileAsString()));

    const auto kick = writeWav(root, "Kick_one-shot.wav", 0.25);
    const auto texture = writeWav(root, "Texture.wav", 2.5);
    ASSERT_TRUE(kick.existsAsFile());
    ASSERT_TRUE(texture.existsAsFile());

    {
        SampleIndexer indexer(root, indexFile, taxonomyFile);
        EXPECT_FALSE(indexer.loadedFromCache());
        ASSERT_TRUE(waitForIdle(indexer));
        EXPECT_EQ(indexer.snapshot().size(), 2u);
        EXPECT_EQ(indexer.lastClassifiedCount(), 2);
        EXPECT_EQ(indexer.lastDurationReadCount(), 1);

        indexer.requestScan(false);
        ASSERT_TRUE(waitForIdle(indexer));
        EXPECT_EQ(indexer.lastClassifiedCount(), 0);
        EXPECT_EQ(indexer.lastDurationReadCount(), 0);

        ASSERT_TRUE(writeWav(root, "Texture.wav", 0.5).existsAsFile());
        indexer.requestScan(false);
        ASSERT_TRUE(waitForIdle(indexer));
        EXPECT_EQ(indexer.lastClassifiedCount(), 1);
        EXPECT_EQ(indexer.lastDurationReadCount(), 1);
        auto currentSnapshot = indexer.snapshot();
        const auto* changed = currentSnapshot.find(texture);
        ASSERT_NE(changed, nullptr);
        EXPECT_EQ(changed->tags.category, SampleCategory::OneShot);

        auto taxonomyText = taxonomyFile.loadFileAsString();
        taxonomyText = taxonomyText.replace("\"minDurationSec\": 2.0",
                                             "\"minDurationSec\": 0.1");
        ASSERT_TRUE(taxonomyFile.replaceWithText(taxonomyText));
        ASSERT_TRUE(taxonomyFile.setLastModificationTime(
            juce::Time::getCurrentTime() + juce::RelativeTime::seconds(2.0)));
        indexer.checkForTaxonomyChanges();
        bool hotReloaded = false;
        for (int attempt = 0; attempt < 400; ++attempt)
        {
            currentSnapshot = indexer.snapshot();
            changed = currentSnapshot.find(texture);
            if (changed != nullptr && changed->tags.category == SampleCategory::Loop
                && !indexer.isScanning())
            {
                hotReloaded = true;
                break;
            }
            juce::Thread::sleep(10);
        }
        ASSERT_TRUE(hotReloaded);
        EXPECT_EQ(indexer.lastClassifiedCount(), 2);
        EXPECT_EQ(indexer.lastDurationReadCount(), 0);
        currentSnapshot = indexer.snapshot();
        changed = currentSnapshot.find(texture);
        ASSERT_NE(changed, nullptr);
        EXPECT_EQ(changed->tags.category, SampleCategory::Loop);

        ASSERT_TRUE(kick.deleteFile());
        indexer.requestScan(false);
        ASSERT_TRUE(waitForIdle(indexer));
        EXPECT_EQ(indexer.snapshot().size(), 1u);

        indexer.requestScan(true);
        ASSERT_TRUE(waitForIdle(indexer));
        EXPECT_EQ(indexer.lastClassifiedCount(), 1);
    }

    ASSERT_TRUE(indexFile.existsAsFile());
    {
        SampleIndexer cached(root, indexFile, taxonomyFile);
        EXPECT_TRUE(waitForIdle(cached));
        EXPECT_TRUE(cached.loadedFromCache());
        EXPECT_EQ(cached.snapshot().size(), 1u);
        EXPECT_EQ(cached.lastClassifiedCount(), 0)
            << "The queued scan must observe the cache before comparing files";
    }

    ASSERT_TRUE(indexFile.replaceWithText("not valid json"));
    {
        SampleIndexer recovered(root, indexFile, taxonomyFile);
        EXPECT_FALSE(recovered.loadedFromCache());
        EXPECT_TRUE(waitForIdle(recovered));
        EXPECT_EQ(recovered.snapshot().size(), 1u);
    }
    EXPECT_TRUE(base.deleteRecursively());
}

TEST(SampleIndexerTests, KeepsUnreadableAudioVisibleAsUnknown)
{
    const auto base = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("sample-indexer-broken-" + juce::Uuid().toString());
    ASSERT_TRUE(base.createDirectory().wasOk());
    const auto root = base.getChildFile("samples");
    ASSERT_TRUE(root.createDirectory().wasOk());
    const auto broken = root.getChildFile("mystery.wav");
    ASSERT_TRUE(broken.replaceWithText("not audio"));

    {
        SampleIndexer indexer(root, base.getChildFile("index.json"),
                              base.getChildFile("missing-taxonomy.json"));
        ASSERT_TRUE(waitForIdle(indexer));
        const auto snapshot = indexer.snapshot();
        const auto* entry = snapshot.find(broken);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->tags.category, SampleCategory::Unknown);
        EXPECT_EQ(entry->tags.instrumentType, SampleInstrumentType::Unknown);
    }
    EXPECT_TRUE(base.deleteRecursively());
}
