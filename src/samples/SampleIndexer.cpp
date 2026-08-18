#include "SampleIndexer.h"

#include "SampleClassifier.h"

#include <algorithm>

class SampleIndexer::CacheLoadJob final : public juce::ThreadPoolJob
{
public:
    explicit CacheLoadJob(SampleIndexer& owner)
        : juce::ThreadPoolJob("Sample index cache load"), owner_(owner)
    {
    }

    JobStatus runJob() override
    {
        auto cachedIndex = owner_.loadCachedIndex();
        if (!shouldExit() && !owner_.stopping_.load(std::memory_order_acquire))
            owner_.finishCacheLoad(std::move(cachedIndex));
        return jobHasFinished;
    }

private:
    SampleIndexer& owner_;
};

class SampleIndexer::ScanJob final : public juce::ThreadPoolJob
{
public:
    ScanJob(SampleIndexer& owner, bool forceFullRescan)
        : juce::ThreadPoolJob("Sample library index"),
          owner_(owner),
          forceFullRescan_(forceFullRescan)
    {
    }

    JobStatus runJob() override
    {
        owner_.runScan(forceFullRescan_, *this);
        return jobHasFinished;
    }

private:
    SampleIndexer& owner_;
    const bool forceFullRescan_;
};

SampleIndexer::SampleIndexer(juce::File sampleRoot,
                             juce::File indexFile,
                             juce::File taxonomyFile)
    : sampleRoot_(std::move(sampleRoot)),
      indexFile_(std::move(indexFile)),
      taxonomyFile_(std::move(taxonomyFile))
{
    formatManager_.registerBasicFormats();
    taxonomyModificationTime_ = taxonomyFile_.existsAsFile()
                                    ? taxonomyFile_.getLastModificationTime().toMilliseconds()
                                    : 0;
    startTimer(2000);

    // Cache parsing can grow without bound with the user's library, so begin
    // it on the single-worker pool. Mark the worker occupied before queuing
    // the normal scan: requestScan() then records a pending scan, guaranteeing
    // cache publication happens first without blocking the message thread.
    {
        std::scoped_lock lock(mutex_);
        jobRunning_ = true;
    }
    scanning_.store(true, std::memory_order_release);
    triggerAsyncUpdate();
    pool_.addJob(new CacheLoadJob(*this), true);
    requestScan(false);
}

SampleIndexer::~SampleIndexer()
{
    stopTimer();
    stopping_.store(true, std::memory_order_release);
    pool_.removeAllJobs(true, 10000);
    cancelPendingUpdate();
}

void SampleIndexer::setChangeCallback(ChangeCallback callback)
{
    std::scoped_lock lock(mutex_);
    changeCallback_ = std::move(callback);
}

void SampleIndexer::requestScan(bool forceFullRescan)
{
    {
        std::scoped_lock lock(mutex_);
        if (jobRunning_)
        {
            scanPending_ = true;
            pendingFullRescan_ = pendingFullRescan_ || forceFullRescan;
            return;
        }
        jobRunning_ = true;
    }
    startQueuedScan(forceFullRescan);
}

void SampleIndexer::startQueuedScan(bool forceFullRescan)
{
    scanning_.store(true, std::memory_order_release);
    triggerAsyncUpdate();
    pool_.addJob(new ScanJob(*this, forceFullRescan), true);
}

SampleIndex SampleIndexer::snapshot() const
{
    std::scoped_lock lock(mutex_);
    return index_;
}

void SampleIndexer::handleAsyncUpdate()
{
    ChangeCallback callback;
    {
        std::scoped_lock lock(mutex_);
        callback = changeCallback_;
    }
    if (callback)
        callback();
}

void SampleIndexer::timerCallback()
{
    checkForTaxonomyChanges();
}

void SampleIndexer::checkForTaxonomyChanges()
{
    const auto current = taxonomyFile_.existsAsFile()
                             ? taxonomyFile_.getLastModificationTime().toMilliseconds()
                             : 0;
    juce::int64 previous = 0;
    {
        std::scoped_lock lock(mutex_);
        previous = taxonomyModificationTime_;
    }
    if (current != previous)
        requestScan(false);
}

void SampleIndexer::runScan(bool forceFullRescan, juce::ThreadPoolJob& job)
{
    auto nextIndex = snapshot();
    const auto taxonomyMtime = taxonomyFile_.existsAsFile()
                                   ? taxonomyFile_.getLastModificationTime().toMilliseconds()
                                   : 0;
    juce::int64 previousTaxonomyMtime = 0;
    {
        std::scoped_lock lock(mutex_);
        previousTaxonomyMtime = taxonomyModificationTime_;
    }
    const bool taxonomyChanged = taxonomyMtime != previousTaxonomyMtime;
    SampleClassifier classifier(SampleTaxonomy::load(taxonomyFile_));

    std::vector<juce::String> retainedPaths;
    int classifiedCount = 0;
    int durationReadCount = 0;
    if (sampleRoot_.isDirectory())
    {
        for (const auto& item : juce::RangedDirectoryIterator(
                 sampleRoot_, true, "*", juce::File::findFiles))
        {
            if (job.shouldExit() || stopping_.load(std::memory_order_acquire))
                return;
            const auto file = item.getFile();
            if (!isSupportedAudioFile(file))
                continue;

            const auto path = file.getFullPathName();
            retainedPaths.push_back(path);
            const auto modificationTime = file.getLastModificationTime().toMilliseconds();
            const auto size = file.getSize();
            const auto* previous = nextIndex.find(file);
            const bool fileChanged = previous == nullptr
                                     || previous->modificationTimeMs != modificationTime
                                     || previous->size != size;
            if (!forceFullRescan && !taxonomyChanged && !fileChanged)
                continue;

            SampleIndexEntry entry;
            entry.file = file;
            entry.modificationTimeMs = modificationTime;
            entry.size = size;
            if (!fileChanged && previous != nullptr)
                entry.durationSeconds = previous->durationSeconds;
            bool durationRead = false;
            entry.tags = classifier.classify(file,
                [this, &entry, &durationReadCount, &durationRead, fileChanged]
                (const juce::File& classifiedFile)
                {
                    if (!fileChanged)
                        return entry.durationSeconds;
                    if (!durationRead)
                    {
                        entry.durationSeconds = readDuration(classifiedFile);
                        durationRead = true;
                        ++durationReadCount;
                    }
                    return entry.durationSeconds;
                });
            nextIndex.set(std::move(entry));
            ++classifiedCount;
        }
    }
    nextIndex.removeMissing(retainedPaths);
    persist(nextIndex);
    finishScan(std::move(nextIndex), classifiedCount, durationReadCount, taxonomyMtime);
}

void SampleIndexer::finishScan(SampleIndex newIndex,
                               int classifiedCount,
                               int durationReadCount,
                               juce::int64 taxonomyModificationTime)
{
    bool restart = false;
    bool forceRestart = false;
    {
        std::scoped_lock lock(mutex_);
        index_ = std::move(newIndex);
        taxonomyModificationTime_ = taxonomyModificationTime;
        jobRunning_ = false;
        restart = scanPending_;
        forceRestart = pendingFullRescan_;
        scanPending_ = false;
        pendingFullRescan_ = false;
        if (restart)
            jobRunning_ = true;
    }
    lastClassifiedCount_.store(classifiedCount, std::memory_order_release);
    lastDurationReadCount_.store(durationReadCount, std::memory_order_release);
    scanning_.store(restart, std::memory_order_release);
    triggerAsyncUpdate();
    if (restart)
        startQueuedScan(forceRestart);
}

void SampleIndexer::finishCacheLoad(std::optional<SampleIndex> cachedIndex)
{
    bool restart = false;
    bool forceRestart = false;
    {
        std::scoped_lock lock(mutex_);
        if (cachedIndex.has_value())
        {
            index_ = std::move(*cachedIndex);
            loadedFromCache_.store(true, std::memory_order_release);
        }
        jobRunning_ = false;
        restart = scanPending_;
        forceRestart = pendingFullRescan_;
        scanPending_ = false;
        pendingFullRescan_ = false;
        if (restart)
            jobRunning_ = true;
    }

    scanning_.store(restart, std::memory_order_release);
    triggerAsyncUpdate();
    if (restart)
        startQueuedScan(forceRestart);
}

bool SampleIndexer::isSupportedAudioFile(const juce::File& file)
{
    static const std::vector<juce::String> extensions {
        ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg"
    };
    const auto extension = file.getFileExtension().toLowerCase();
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

std::optional<double> SampleIndexer::readDuration(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples < 0)
        return std::nullopt;
    return static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
}

std::optional<SampleIndex> SampleIndexer::loadCachedIndex() const
{
    if (!indexFile_.existsAsFile())
        return std::nullopt;
    const auto document = juce::JSON::parse(indexFile_.loadFileAsString());
    return SampleIndex::fromJson(document, sampleRoot_);
}

void SampleIndexer::persist(const SampleIndex& index) const
{
    const auto parent = indexFile_.getParentDirectory();
    if (!parent.exists() && parent.createDirectory().failed())
        return;
    juce::TemporaryFile temporary(indexFile_);
    if (!temporary.getFile().replaceWithText(
            juce::JSON::toString(index.toJson(sampleRoot_), true)))
        return;
    if (!temporary.overwriteTargetFileWithTemporary())
        juce::Logger::writeToLog("[SampleIndexer] Failed to persist "
                                 + indexFile_.getFullPathName());
}
