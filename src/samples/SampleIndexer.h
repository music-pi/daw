#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "SampleIndex.h"
#include "SampleTaxonomy.h"

class SampleIndexer : private juce::AsyncUpdater,
                      private juce::Timer
{
public:
    using ChangeCallback = std::function<void()>;

    SampleIndexer(juce::File sampleRoot,
                  juce::File indexFile,
                  juce::File taxonomyFile);
    ~SampleIndexer() override;

    void setChangeCallback(ChangeCallback callback);
    void requestScan(bool forceFullRescan = false);
    void checkForTaxonomyChanges();

    SampleIndex snapshot() const;
    bool isScanning() const noexcept { return scanning_.load(std::memory_order_acquire); }
    int lastClassifiedCount() const noexcept
    {
        return lastClassifiedCount_.load(std::memory_order_acquire);
    }
    int lastDurationReadCount() const noexcept
    {
        return lastDurationReadCount_.load(std::memory_order_acquire);
    }

    const juce::File& sampleRoot() const noexcept { return sampleRoot_; }
    const juce::File& indexFile() const noexcept { return indexFile_; }
    bool loadedFromCache() const noexcept
    {
        return loadedFromCache_.load(std::memory_order_acquire);
    }

private:
    class CacheLoadJob;
    class ScanJob;

    void handleAsyncUpdate() override;
    void timerCallback() override;
    void runScan(bool forceFullRescan, juce::ThreadPoolJob& job);
    void finishCacheLoad(std::optional<SampleIndex> cachedIndex);
    void finishScan(SampleIndex newIndex,
                    int classifiedCount,
                    int durationReadCount,
                    juce::int64 taxonomyModificationTime);
    void startQueuedScan(bool forceFullRescan);
    static bool isSupportedAudioFile(const juce::File& file);
    std::optional<double> readDuration(const juce::File& file);
    std::optional<SampleIndex> loadCachedIndex() const;
    void persist(const SampleIndex& index) const;

    const juce::File sampleRoot_;
    const juce::File indexFile_;
    const juce::File taxonomyFile_;
    juce::AudioFormatManager formatManager_;
    juce::ThreadPool pool_ { 1 };

    mutable std::mutex mutex_;
    SampleIndex index_;
    ChangeCallback changeCallback_;
    bool jobRunning_ { false };
    bool scanPending_ { false };
    bool pendingFullRescan_ { false };
    juce::int64 taxonomyModificationTime_ { 0 };
    std::atomic<bool> loadedFromCache_ { false };

    std::atomic<bool> stopping_ { false };
    std::atomic<bool> scanning_ { false };
    std::atomic<int> lastClassifiedCount_ { 0 };
    std::atomic<int> lastDurationReadCount_ { 0 };
};
