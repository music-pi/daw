#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

class UiHost;

class MainWindow : public juce::DocumentWindow
{
public:
  explicit MainWindow (juce::String name);
  ~MainWindow() override; 
  void closeButtonPressed() override;
private:
  std::unique_ptr<UiHost> ui;
};
