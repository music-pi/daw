#include "DataPaths.h"

#include <algorithm>

namespace
{
constexpr const char* kRootFolder = "maschinepi";
constexpr const char* kSamplesFolder = "samples";
constexpr const char* kProjectsFolder = "projects";
constexpr const char* kConfigFileName = "maschinepi.conf";

juce::File ensureDirectory(const juce::File& directory)
{
    if (!directory.exists())
        directory.createDirectory();
    return directory;
}

void ensureConfigFile(const juce::File& file,
                      const juce::File& samplesDir,
                      const juce::File& projectsDir)
{
    if (file.existsAsFile())
        return;

    juce::StringArray lines;
    lines.add("# MusicPI configuration");
    lines.add("# Automatically generated on first launch");
    lines.add("samples_dir=" + samplesDir.getFullPathName());
    lines.add("projects_dir=" + projectsDir.getFullPathName());

    file.replaceWithText(lines.joinIntoString("\n"));
}
} // namespace

namespace DataPaths
{
juce::File getRoot()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    return home.getChildFile(kRootFolder);
}

juce::File getSamplesDir()
{
    return getRoot().getChildFile(kSamplesFolder);
}

juce::File getProjectsDir()
{
    return getRoot().getChildFile(kProjectsFolder);
}

juce::File getConfigFile()
{
    return getRoot().getChildFile(kConfigFileName);
}

void ensureStructure()
{
    auto root = ensureDirectory(getRoot());
    auto samples = ensureDirectory(getSamplesDir());
    auto projects = ensureDirectory(getProjectsDir());
    ensureConfigFile(getConfigFile(), samples, projects);
}

std::vector<juce::File> findRecentProjects(size_t maxCount)
{
    struct Entry
    {
        juce::File file;
        juce::Time modified;
    };

    std::vector<Entry> entries;
    auto projectsDir = getProjectsDir();

    if (!projectsDir.exists())
        return {};

    for (const auto& entry : juce::RangedDirectoryIterator(projectsDir, true, "*", juce::File::findFiles))
    {
        auto file = entry.getFile();
        if (!file.existsAsFile())
            continue;

        const auto ext = file.getFileExtension().toLowerCase();
        if (ext != ".mpi" && ext != ".tracktionedit")
            continue;

        entries.push_back({ file, file.getLastModificationTime() });
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b)
              {
                  return a.modified > b.modified;
              });

    std::vector<juce::File> result;
    for (size_t i = 0; i < entries.size() && i < maxCount; ++i)
        result.push_back(entries[i].file);

    return result;
}
} // namespace DataPaths

