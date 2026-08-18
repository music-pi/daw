#pragma once

#include <map>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

class SampleTaxonomy
{
public:
    using KeywordMap = std::map<juce::String, std::vector<juce::String>>;

    static SampleTaxonomy defaults();
    static std::optional<SampleTaxonomy> fromJson(const juce::var& document);
    static SampleTaxonomy load(const juce::File& file, bool* usedFallback = nullptr);
    static juce::File findDefaultFile();

    double minimumLoopDurationSeconds() const noexcept { return minimumLoopDurationSeconds_; }
    const KeywordMap& instrumentKeywords() const noexcept { return instrumentKeywords_; }
    const KeywordMap& subTypeKeywords() const noexcept { return subTypeKeywords_; }
    const std::vector<juce::String>& loopKeywords() const noexcept { return loopKeywords_; }
    const std::vector<juce::String>& oneShotKeywords() const noexcept { return oneShotKeywords_; }
    const juce::String& bpmRegex() const noexcept { return bpmRegex_; }
    const juce::String& keyRegex() const noexcept { return keyRegex_; }

private:
    double minimumLoopDurationSeconds_ { 2.0 };
    KeywordMap instrumentKeywords_;
    KeywordMap subTypeKeywords_;
    std::vector<juce::String> loopKeywords_;
    std::vector<juce::String> oneShotKeywords_;
    juce::String bpmRegex_;
    juce::String keyRegex_;
};
