#pragma once
#include <array>
#include <atomic>
#include <deque>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "../engine/PluginCatalog.h"
#include "../control/ControllerHost.h"
#include "../input/InputManager.h"
#include "../input/MidiInputHandler.h"
#include "../input/KeyboardInputHandler.h"
#include "../input/ControllerInputHandler.h"
#include "widget/WindowManager.h"
#include "widget/FileBrowserWidget.h"
#include "widget/PluginBrowserWidget.h"
#include "theme/MaschineLookAndFeel.h"
#include "components/OptionDialogComponent.h"
#include "IRefreshable.h"

class PadDetailsWidget;
class AudioEditorWidget;
class AudioRecorderWidget;
class PatternWidget;
class SampleBrowserComponent;
class SamplePreviewPlayer;
class SampleIndexer;

class UiHost : public juce::Component,
               public IRefreshable,
               private juce::Timer,
               private AudioEngine::Listener
{
public:
  UiHost();
  ~UiHost() override;
  void resized() override;
  bool keyPressed (const juce::KeyPress& key) override;
  bool keyStateChanged(bool isKeyDown) override;

  /** Marks the display as needing a push on the next tick. Safe to call from any thread. */
  void requestDisplayRefresh() noexcept override;
  void requestDisplayRefresh(unsigned panelMask) noexcept override;

  void showFileDialog(FileBrowserWidget::Mode mode);
  void dismissFileDialog();
  void openFilePreviewOnRight();
  void pushLoadConfirm(const juce::File& file);
  void pushDeleteConfirm(const juce::File& file, FileBrowserWidget* browser);

  /** Open PluginBrowserWidget as a modal picker. The onPicked callback fires
      with the chosen plugin description (or nothing, on cancel) and the
      browser is dismissed before the callback returns. */
  void showPluginBrowser(PluginBrowserWidget::Filter filter,
                         std::function<void(const juce::PluginDescription&)> onPicked);
  void dismissPluginBrowser();

private:
  AudioEngine       audio;
  PluginCatalog     pluginCatalog;
  InputManager      inputManager;
  MidiInputHandler  midiInput;
  KeyboardInputHandler keyboardInput;
  ControllerInputHandler controllerInput;
  ControllerHost    control;
  WindowManager     windowManager_;

  MaschineLookAndFeel lookAndFeel;
  bool keyboardShiftActive { false };
  bool keyboardSelectActive { false };
  bool replaceMode_ { false };
  bool clickWasEnabled_ { false };
  bool fixedVelocity_ { false };
  bool sixteenVelocities_ { false };
  std::deque<int64_t> tapTempoTimes_;

  // Keyboard mode: when active, pads 1..16 send chromatic MIDI notes from
  // keyboardModeBasePitch_ up to +15 to the currently-selected pad's track
  // instead of triggering samples. Toggled via the "keyboard" hardware
  // button; base pitch shifted via Shift + pad 13/14 (semitone) or 15/16
  // (octave). A KeyboardWidget opens on the Right panel while active.
  bool keyboardModeActive_ { false };
  int keyboardModeBasePitch_ { 36 };  // C2
  std::unique_ptr<Widget> keyboardStashRight_;

  // Pending MIDI-record state per pad: when a key is pressed in keyboard
  // mode while record is armed + transport playing, we remember the beat
  // position + pitch so on release we can add a note spanning the hold
  // duration to the active pad's MidiClip.
  struct PendingRecord { bool active; double startBeat; int pitch; int velocity; };
  std::array<PendingRecord, 16> keyboardPendingRec_ {};
  std::array<int, 16> keyboardHeldPitches_ {
      -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1
  };

  void enterKeyboardMode();
  void exitKeyboardMode();
  std::unique_ptr<PatternWidget> createPatternWidget();
  void openPianoRollForSelectedChannel();
  void updateKeyboardWidget();
  void updateKeyboardLed();
  void updateFixedVelocityLed();
  void updateModeLeds();
  juce::String lastModeLedSignature_;
  int modeLedRefreshTicks_ { 0 };
  juce::String describeCurrentPadSource() const;
  class KeyboardWidget* keyboardWidgetPtr_ { nullptr };

  // Plugin-editor toggle: pluginInstance button opens the active keyboard
  // plugin's UI on the Left panel; pressing again closes it.
  bool pluginEditorOpen_ { false };
  std::unique_ptr<Widget> pluginEditorStashLeft_;
  void togglePluginEditor();

  // Arpeggiator controls wired to the noteRepeatArp button.
  void toggleArp();
  void showArpDialog();
  void updateArpLed();

  // Piano-roll toggle: navPush in keyboard mode opens the piano roll for
  // the selected instrument channel; navPush again closes it.
  std::unique_ptr<Widget> pianoRollStashLeft_;

  void timerCallback() override;
  void editAboutToBeReplaced() override;
  void editReplaced() override;
  void notifyStashedWidgets(bool beforeReplacement);
  void ensureSampleIndexer();
  void showSampleBrowser();
  void hideSampleBrowser();
  void showOptionDialog(const juce::String& title,
                        const juce::String& message,
                        const std::vector<OptionDialogComponent::Button>& buttons);
  void hideOptionDialog();
  void showAudioSettingsDialog();
  void performUndo(const juce::String& source);
  void performRedo(const juce::String& source);
  void updateKeyboardShiftState(bool active);
  void updateKeyboardSelectState(bool active);
  void emergencyPanic();
  std::unique_ptr<PadDetailsWidget> createPadDetailsWidget();
  void openPadDetailsForSelection();
  std::unique_ptr<AudioEditorWidget> createAudioEditorWidget();
  void openSamplingWorkflow();
  void showAudioRecorder();
  void dismissAudioRecorder();
  void openSlicedPadWorkflow(int sliceCount);
  void showSliceDetails(AudioEditorWidget& editor);
  void hideSliceDetails();
  void openBrowserForPad(int padId, bool addLayer = false);

  class FileDialogListener;
  std::unique_ptr<FileDialogListener> fileDialogListener_;
  juce::ValueTree fileDialogState_;

  class PluginBrowserListener;
  std::unique_ptr<PluginBrowserListener> pluginBrowserListener_;

  // Stashed widgets from before the File dialog was opened. Restored on
  // dismiss so the user returns to the view they were in (rather than
  // landing at wallpaper).
  std::unique_ptr<Widget> stashedLeft_;
  std::unique_ptr<Widget> stashedRight_;

  // Plugin-browser stash — mirrors the File dialog pattern so the user lands
  // back on their previous view on dismiss.
  std::unique_ptr<Widget> pluginBrowserStashLeft_;
  std::unique_ptr<Widget> pluginBrowserStashRight_;

  // Shift + Sampling recording suite stash. The recorder spans both panels;
  // dismissing it restores the exact previous layout.
  std::unique_ptr<Widget> audioRecorderStashLeft_;
  std::unique_ptr<Widget> audioRecorderStashRight_;
  std::unique_ptr<Widget> sliceDetailsStashRight_;

  // WindowManager owns the browser and may replace its slot in response to a
  // global view button. SafePointer observes that destruction so index updates
  // and later Browser presses cannot dereference a stale component.
  juce::Component::SafePointer<SampleBrowserComponent> sampleBrowser_;
  std::unique_ptr<Widget> sampleBrowserStashLeft_;
  std::unique_ptr<SampleIndexer> sampleIndexer_;
  std::unique_ptr<OptionDialogComponent> optionDialog;
  juce::Image frameBuffer_;
  std::unique_ptr<SamplePreviewPlayer> previewPlayer_;
  std::atomic<unsigned> displayDirtyMask_ { 3u };

  struct PerformanceWindow
  {
    double startedMs { 0.0 };
    double callbackMs { 0.0 };
    double updateMs { 0.0 };
    double barRefreshMs { 0.0 };
    double widgetTickMs { 0.0 };
    double hardwareFlushMs { 0.0 };
    double modeLedMs { 0.0 };
    double paintMs { 0.0 };
    double submitMs { 0.0 };
    double maxCallbackMs { 0.0 };
    int ticks { 0 };
    int frames { 0 };
  } performanceWindow_;
  bool performanceLoggingEnabled_ { false };
};
