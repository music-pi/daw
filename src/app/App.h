#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

class MainWindow;

class App : public juce::JUCEApplication
{
public:
  const juce::String getApplicationName() override  { return "MusicPI"; }
  const juce::String getApplicationVersion() override { return "0.1.0"; }
  void initialise (const juce::String&) override;
  void shutdown() override;

  tracktion::engine::Engine& getEngine() { return *engine; }

private:
  std::unique_ptr<MainWindow> mainWindow;
  std::unique_ptr<tracktion::engine::Engine> engine;
};
