#include "MainWindow.h"
#include "../ui/UiHost.h"

MainWindow::MainWindow (juce::String name)
: DocumentWindow(name, juce::Colours::black, DocumentWindow::allButtons)
{
  DBG("MainWindow ctor");
#if ! JUCE_HEADLESS
  setUsingNativeTitleBar(true);
#endif
  ui = std::make_unique<UiHost>();
  // unique_ptr owns the UiHost; tell JUCE not to delete it
  setContentNonOwned(ui.get(), false);
#if JUCE_HEADLESS
  // Headless: set bounds directly without querying desktop
  setBounds(0, 0, 960, 272);
#else
  centreWithSize(960, 272);
#endif
  setResizable(false, false);
  setVisible(true);  // Required for component hierarchy initialization
}

MainWindow::~MainWindow() = default;

void MainWindow::closeButtonPressed()
{
  juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
