// SPDX-License-Identifier: GPL-3.0-only
//
// MusicPI — headless DAW for the Native Instruments Maschine MK3.
// Copyright (C) 2026 Sebastian Hines
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 3 as published by
// the Free Software Foundation. See the LICENSE file in the repository root.
// MusicPI links JUCE and Tracktion Engine under their GPLv3 terms.

#include "App.h"
#include "MainWindow.h"
#include "DataPaths.h"
#include <juce_core/juce_core.h>
#include <tracktion_graph/tracktion_graph.h>

#include <optional>

namespace
{
class MaschineEngineBehaviour final : public tracktion::engine::EngineBehaviour
{
public:
    int getNumberOfCPUsToUseForAudio() override
    {
        const auto available = juce::jmax(1, juce::SystemStats::getNumCpus());
        const auto requested = juce::SystemStats::getEnvironmentVariable(
            "MASCHINEPI_AUDIO_CPUS", {}).getIntValue();

        // Tracktion creates one graph worker fewer than this value. Four audio
        // CPUs provide three workers plus the audio callback while avoiding the
        // idle spin cost of one worker per logical CPU on larger hosts.
        return requested > 0 ? juce::jlimit(1, available, requested)
                             : juce::jmin(4, available);
    }
};

void configureTracktionThreadPool()
{
    const auto requested = juce::SystemStats::getEnvironmentVariable(
        "MASCHINEPI_AUDIO_THREAD_POOL", "realtime").trim().toLowerCase();

    using Strategy = tracktion::graph::ThreadPoolStrategy;
    std::optional<Strategy> strategy;
    if (requested == "condition" || requested == "condition-variable")
        strategy = Strategy::conditionVariable;
    else if (requested == "realtime")
        strategy = Strategy::realTime;
    else if (requested == "hybrid")
        strategy = Strategy::hybrid;
    else if (requested == "semaphore")
        strategy = Strategy::semaphore;
    else if (requested == "lightweight-semaphore")
        strategy = Strategy::lightweightSemaphore;
    else if (requested == "lightweight-semaphore-hybrid")
        strategy = Strategy::lightweightSemHybrid;

    if (!strategy.has_value())
    {
        juce::Logger::writeToLog(
            "[App] Ignoring unknown MASCHINEPI_AUDIO_THREAD_POOL=" + requested);
        return;
    }

    tracktion::engine::EditPlaybackContext::setThreadPoolStrategy(
        static_cast<int>(*strategy));
    juce::Logger::writeToLog("[App] Tracktion audio thread pool: " + requested);
}
} // namespace

// Forward declaration - function is in Mk3Device.cpp
void setInputTracingEnabled(bool enabled);

void App::initialise (const juce::String& commandLine)
{
  // Parse command-line arguments
  juce::StringArray args = juce::JUCEApplication::getCommandLineParameterArray();

  DataPaths::ensureStructure();
  configureTracktionThreadPool();
  
  // Check for --trace-input flag
  if (args.contains("--trace-input"))
  {
    setInputTracingEnabled(true);
    juce::Logger::writeToLog("[App] Input tracing enabled via --trace-input flag");
  }
  
  auto behaviour = std::make_unique<MaschineEngineBehaviour>();
  const auto audioCpus = behaviour->getNumberOfCPUsToUseForAudio();
  juce::Logger::writeToLog("[App] Tracktion audio CPUs: " + juce::String(audioCpus));
  engine = std::make_unique<tracktion::engine::Engine>(
      getApplicationName(), nullptr, std::move(behaviour));
  mainWindow = std::make_unique<MainWindow>(getApplicationName());
}

void App::shutdown()
{
  mainWindow.reset();
  engine->getDeviceManager().closeDevices();
  engine.reset();
}

START_JUCE_APPLICATION (App)
