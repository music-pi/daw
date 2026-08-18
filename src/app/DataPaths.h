#pragma once

#include <juce_core/juce_core.h>

namespace DataPaths
{
juce::File getRoot();
juce::File getSamplesDir();
juce::File getProjectsDir();
juce::File getConfigFile();
std::vector<juce::File> findRecentProjects(size_t maxCount);
void ensureStructure();
} // namespace DataPaths


