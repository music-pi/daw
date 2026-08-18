#pragma once

#include <functional>
#include <optional>

#include <juce_core/juce_core.h>

#include "SampleTaxonomy.h"
#include "SampleTypes.h"

class SampleClassifier
{
public:
    using DurationProvider = std::function<std::optional<double>(const juce::File&)>;

    explicit SampleClassifier(SampleTaxonomy taxonomy);

    SampleTags classify(const juce::File& file,
                        const DurationProvider& durationProvider) const;

private:
    SampleTaxonomy taxonomy_;
    SampleTaxonomy::KeywordMap instrumentKeywords_;
    SampleTaxonomy::KeywordMap subTypeKeywords_;
    std::vector<juce::String> loopKeywords_;
    std::vector<juce::String> oneShotKeywords_;
};
