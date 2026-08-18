#pragma once

#include <map>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "SampleTypes.h"

struct SampleIndexEntry
{
    juce::File file;
    juce::int64 modificationTimeMs { 0 };
    juce::int64 size { 0 };
    std::optional<double> durationSeconds;
    SampleTags tags;
};

struct SampleCategoryNode
{
    juce::String id;
    juce::String label;
    juce::String breadcrumb;
    juce::File file;
    bool isFile { false };
    std::vector<SampleCategoryNode> children;
};

class SampleIndex
{
public:
    using EntryMap = std::map<juce::String, SampleIndexEntry>;

    const EntryMap& entries() const noexcept { return entries_; }
    size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

    const SampleIndexEntry* find(const juce::File& file) const;
    void set(SampleIndexEntry entry);
    void removeMissing(const std::vector<juce::String>& retainedPaths);
    void clear();

    std::vector<SampleCategoryNode> buildCategoryTree() const;

    juce::var toJson(const juce::File& root) const;
    static std::optional<SampleIndex> fromJson(const juce::var& document,
                                               const juce::File& expectedRoot);

private:
    EntryMap entries_;
};
