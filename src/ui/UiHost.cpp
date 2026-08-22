#include "UiHost.h"

#include <cmath>

#include "tracktion_engine/tracktion_engine.h"
#include "../app/App.h"
#include "../control/HardwareConstants.h"
#include "../engine/Arpeggiator.h"
#include "../engine/MidiConstants.h"
#include "../engine/SamplerInstrument.h"
#include "../engine/SamplePreviewPlayer.h"
#include "../engine/commands/LoadSampleCommand.h"
#include "../samples/SampleIndexer.h"
#include "components/SampleBrowserComponent.h"
#include "widget/FileBrowserWidget.h"
#include "widget/FilePreviewWidget.h"
#include "widget/FileDialogStateKeys.h"
#include "components/OptionDialogComponent.h"
#include "widget/ChannelDetailsWidget.h"
#include "widget/EqualiserWidget.h"
#include "widget/MixerWidget.h"
#include "widget/PluginBrowserWidget.h"
#include "widget/ArpDialog.h"
#include "widget/PluginEditorWidget.h"
#include "../engine/ChannelInsertUtils.h"
#include "../engine/commands/InsertBuiltInPluginCommand.h"
#include "widget/KeyboardWidget.h"
#include "widget/PianoRollWidget.h"
#include "../engine/KeyboardInstrumentBank.h"
#include "../engine/commands/InsertEffectPluginCommand.h"
#include "../engine/commands/SetPadInstrumentCommand.h"
#include "../engine/commands/ClearPadInstrumentCommand.h"
#include "../engine/commands/SetTempoCommand.h"
#include "widget/PadDetailsWidget.h"
#include "widget/PadOverviewWidget.h"
#include "widget/PatternWidget.h"
#include "widget/AudioEditorWidget.h"
#include "widget/AudioRecorderWidget.h"
#include "widget/SliceDetailsWidget.h"
#include "widget/ArrangerWidget.h"
#include "widget/SettingsDialog.h"
#include "widget/ConfirmDialog.h"
#include "components/mixer/MixerUtils.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "../app/DataPaths.h"
#include <UiBinaryData.h>

#include <array>
#include <optional>

namespace
{
struct PluginListLocator
{
    bool isMaster { false };
    te::EditItemID ownerTrackId;
};

std::optional<PluginListLocator> locatePluginList(AudioEngine& audio, te::PluginList* list)
{
    auto* edit = audio.getEdit();
    if (edit == nullptr || list == nullptr)
        return std::nullopt;
    if (list == &edit->getMasterPluginList())
        return PluginListLocator { true, {} };
    if (auto* track = list->getOwnerTrack())
        return PluginListLocator { false, track->itemID };
    return std::nullopt;
}

te::PluginList* resolvePluginList(AudioEngine& audio, const PluginListLocator& locator)
{
    auto* edit = audio.getEdit();
    if (edit == nullptr)
        return nullptr;
    if (locator.isMaster)
        return &edit->getMasterPluginList();
    if (auto* track = te::findTrackForID(*edit, locator.ownerTrackId))
        return &track->pluginList;
    return nullptr;
}
} // namespace

UiHost::UiHost()
    : audio(static_cast<App*>(juce::JUCEApplication::getInstance())->getEngine())
    , pluginCatalog(audio.getEngine())
    , inputManager()
    , midiInput(inputManager, audio.getAudioDeviceManager())
    , keyboardInput(inputManager)
    , controllerInput(inputManager)
    , control(&inputManager)
{
  performanceLoggingEnabled_ = juce::SystemStats::getEnvironmentVariable(
      "MASCHINEPI_LOG_UI_PERF", "0").trim().equalsIgnoreCase("1");
  performanceWindow_.startedMs = juce::Time::getMillisecondCounterHiRes();

  setLookAndFeel(&lookAndFeel);
  setSize(UiTheme::kTotalWidth, UiTheme::kPanelHeight);
  addAndMakeVisible(windowManager_);
  windowManager_.setBounds(0, 0, UiTheme::kTotalWidth, UiTheme::kPanelHeight);

  audio.initialiseAudioDevice(48000, 128);
  audio.createEmptyEdit();
  ensureSampleIndexer();

  // Apply the persisted block size, or force the 128-sample default through
  // PipeWire as well as the JUCE client. Without the explicit metadata update
  // a fresh install could render 128-sample client blocks into a 256-sample
  // physical sink quantum, silently doubling end-to-end output latency.
  {
      auto settings = audio.getSettingsState();
      int blockSize = 128;
      if (settings.isValid() && settings.hasProperty("audioBlockSize"))
          blockSize = static_cast<int>(settings.getProperty("audioBlockSize"));
      if (blockSize != 128 && blockSize != 256 && blockSize != 512
          && blockSize != 1024 && blockSize != 2048)
          blockSize = 128;
      audio.setBufferSize(blockSize);
      if (settings.isValid() && settings.hasProperty("meterSkewCentreDb"))
          MixerUtils::setMeterCentreDb(static_cast<int>(settings.getProperty("meterSkewCentreDb")));
  }

  audio.getUndoManager().setMaxNumberOfStoredUnits(100, 0);
  audio.enableClick(false);

  // Wire WindowManager into the system
  windowManager_.setAudioEngine(&audio);
  windowManager_.setControllerHost(&control);
  windowManager_.initSystemWidgets();
  audio.addListener(this);
  control.setWindowManager(&windowManager_);

  // Load wallpaper from embedded asset. Function-local static so the JPEG
  // decode runs once per process instead of per UiHost construction.
  {
      static const juce::Image wallpaper =
          juce::ImageFileFormat::loadFrom(UiBinaryData::bgdefault2_png,
                                          static_cast<size_t>(UiBinaryData::bgdefault2_pngSize));
      if (wallpaper.isValid())
          windowManager_.setWallpaper(wallpaper);
  }

  control.bindTransport(audio);

  // Screen toggle buttons -- registered as Global InputManager handlers.
  // Switching views dismisses any open dialog (e.g. Settings) so it doesn't
  // stay stuck over the new view.
  control.setButtonActive("pattern", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "pattern",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          if (keyboardModeActive_) exitKeyboardMode();
          windowManager_.dismissAllDialogs();
          windowManager_.open(createPatternWidget(), DisplaySide::Left);
          event.consumed = true;
      });

  // Open the mixer + details pair on the two panels. WindowManager::open()
  // sets focus to the most-recently-opened side, so opening the right side
  // first and the left second leaves focus on the mixer — required for
  // nav/arrow buttons to reach it.
  auto openMixerPair = [this](
      std::optional<te::EditItemID> initialGroup,
      std::optional<te::EditItemID> initialChannel) {
      windowManager_.dismissAllDialogs();
      auto details = std::make_unique<ChannelDetailsWidget>();
      details->setOnAddInsertRequested([this](te::PluginList* targetList) {
          const auto locator = locatePluginList(audio, targetList);
          if (!locator.has_value())
              return;
          juce::Component::SafePointer<UiHost> safeThis(this);
          showPluginBrowser(PluginBrowserWidget::Filter::Effects,
              [safeThis, locator](const juce::PluginDescription& desc) mutable {
                  if (safeThis == nullptr)
                      return;
                  auto* targetList = resolvePluginList(safeThis->audio, *locator);
                  if (targetList == nullptr)
                      return;
                  const int insertAt = ChannelInsertUtils::insertionIndex(*targetList);
                  safeThis->audio.getUndoManager().beginNewTransaction();
                  safeThis->audio.getUndoManager().perform(
                      new InsertEffectPluginCommand(safeThis->audio, *targetList, desc, insertAt));
              });
      });
      details->setOnEqRequested(
          [this](te::PluginList* targetList, te::EqualiserPlugin*) {
              const auto locator = locatePluginList(audio, targetList);
              if (!locator.has_value())
                  return;

              // Opening the editor replaces ChannelDetailsWidget on the right.
              // Defer until the option callback has unwound so it never deletes
              // the widget that is currently dispatching the button press.
              juce::Component::SafePointer<UiHost> safeThis(this);
              juce::MessageManager::callAsync([safeThis, locator]() mutable {
                  if (safeThis == nullptr)
                      return;
                  auto* targetList = resolvePluginList(safeThis->audio, *locator);
                  if (targetList == nullptr)
                      return;
                  auto* equaliser = ChannelInsertUtils::findEqualiser(*targetList);
                  if (equaliser == nullptr)
                  {
                      const int insertAt = ChannelInsertUtils::insertionIndex(*targetList);
                      auto& undo = safeThis->audio.getUndoManager();
                      undo.beginNewTransaction("Add Channel EQ");
                      if (!undo.perform(new InsertBuiltInPluginCommand(
                              safeThis->audio, *targetList,
                              te::EqualiserPlugin::xmlTypeName, insertAt)))
                          return;
                      equaliser = ChannelInsertUtils::findEqualiser(*targetList);
                  }

                  if (equaliser == nullptr)
                      return;

                  const auto* owner = equaliser->getOwnerTrack();
                  const juce::String channelName = owner != nullptr ? owner->getName()
                                                                     : juce::String("Master");
                  safeThis->windowManager_.open(
                      std::make_unique<EqualiserWidget>(*equaliser, channelName),
                      DisplaySide::Right);
              });
          });
      windowManager_.open(std::move(details), DisplaySide::Right);
      windowManager_.open(
          std::make_unique<MixerWidget>(initialGroup, initialChannel),
          DisplaySide::Left);
  };

  control.setButtonActive("mixer", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "mixer",
      [this, openMixerPair](InputEvent& event) {
          if (! event.isPressed()) return;
          std::optional<te::EditItemID> initialGroup;
          std::optional<te::EditItemID> initialChannel;
          if (keyboardModeActive_)
          {
              auto& bank = audio.getKeyboardBank();
              if (auto* folder = bank.getFolder())
                  initialGroup = folder->itemID;
              if (auto* track = bank.getTrack())
                  initialChannel = track->itemID;
              exitKeyboardMode();
          }
          openMixerPair(initialGroup, initialChannel);
          event.consumed = true;
      });

  control.setButtonActive("sampling", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "sampling",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          if (event.isShift())
          {
              if (keyboardModeActive_)
                  exitKeyboardMode();
              windowManager_.dismissAllDialogs();
              showAudioRecorder();
              event.consumed = true;
              return;
          }
          if (keyboardModeActive_)
          {
              // The AudioEditor doesn't yet have a keyboard-mode-aware input
              // story; block entry until that's designed. Toast so the user
              // knows why nothing happened.
              windowManager_.showToast(DisplaySide::Right,
                  ToastKind::Info, "Disabled in Keyboard mode");
              event.consumed = true;
              return;
          }
          windowManager_.dismissAllDialogs();
          openSamplingWorkflow();
          event.consumed = true;
      });

  control.setButtonActive("arranger", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "arranger",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          if (keyboardModeActive_) exitKeyboardMode();
          windowManager_.dismissAllDialogs();
          windowManager_.open(std::make_unique<ArrangerWidget>(), DisplaySide::Left);
          event.consumed = true;
      });

  control.setButtonActive("fileSave", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "fileSave",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;

          const bool shift = event.isShift();

          if (shift)
          {
              auto currentFile = audio.getCurrentProjectFile();
              if (currentFile.existsAsFile())
              {
                  if (audio.saveCurrentProject())
                      windowManager_.showToast(windowManager_.getFocus(), ToastKind::Success, "Saved");
                  else
                      windowManager_.showToast(windowManager_.getFocus(), ToastKind::Error, "Save failed");
              }
              else
              {
                  showFileDialog(FileBrowserWidget::Mode::SaveAs);
              }
          }
          else
          {
              auto* left = windowManager_.getWidget(DisplaySide::Left);
              const bool open = (left != nullptr && left->describe().id == "file_browser");
              if (open) dismissFileDialog();
              else      showFileDialog(FileBrowserWidget::Mode::Browse);
          }
          event.consumed = true;
      });

  control.setButtonActive("recCountIn", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "recCountIn",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          // The dedicated audio recorder owns REC while its suite is open.
          // Leave this event unconsumed so ControllerHost forwards the
          // original pressed event to the fullscreen widget.
          if (audio.isAudioCaptureSuiteActive())
              return;
          audio.toggleRecordArm();
          if (!audio.isRecordArmed())
              replaceMode_ = false;
          // Refresh once immediately so the transport LED and the focused
          // view's armed indicator do not wait for playback (or even the next
          // periodic UI tick) before reflecting the new state.
          windowManager_.tickActiveWidgets();
          windowManager_.flushHardwareState();
          event.consumed = true;
      });

  // Shift + Mute/Choke is an engine-wide emergency all-notes-off. Leave the
  // unshifted press untouched so MixerWidget and pad choke interactions keep
  // their normal behaviour.
  control.setButtonActive("muteChoke", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "",
      "muteChoke",
      [this](InputEvent& event) {
          if (! event.isPressed() || ! event.isShift())
              return;
          emergencyPanic();
          event.consumed = true;
      });

  // 4D encoder rotation behaviour, in priority order:
  //   1. PatternWidget in step-mode: cycle the selected step.
  //   2. Keyboard mode + PatternWidget on Left: navigate the instrument
  //      channel list (turn = channel up/down) + mirror into the keyboard
  //      bank so the active instrument stays in sync.
  //   3. Fallback: not consumed, falls through to synthesised navUp/navDown
  //      on ControllerHost — default channel navigation for pad mode.
  inputManager.addStepperHandler(InputManager::HandlerPriority::Global, "",
      [this](InputEvent& event) {
          auto* pw = dynamic_cast<PatternWidget*>(windowManager_.getWidget(DisplaySide::Left));
          if (pw == nullptr)
              return;

          const int dir = event.stepperDirection();
          if (dir == 0)
              return;

          // Tempo is a global modifier and must take priority over Pattern
          // step scrubbing while the hardware Tempo button is held.
          if (windowManager_.isTempoHeld())
          {
              controller_events::ButtonEvent nav {
                  dir > 0 ? "navDown" : "navUp", true, event.isShift() };
              windowManager_.handleButtonEvent(nav);
              event.consumed = true;
              return;
          }

          if (pw->isStepModeActive())
          {
              pw->cycleSelectedStep(dir);
              windowManager_.refreshBars();
              event.consumed = true;
              return;
          }

          if (keyboardModeActive_)
          {
              pw->navigateChannel(dir);
              // PatternWidget's selectedChannelPadId_ is the bank slot index in
              // Instruments mode, so a direct pass-through keeps the bank in
              // sync.
              auto& bank = audio.getKeyboardBank();
              bank.setActiveSlot(pw->getSelectedChannelPadId());
              updateKeyboardWidget();
              windowManager_.refreshBars();
              event.consumed = true;
          }
      });

  // navPush remains a second route to the same editor exposed as D3 in
  // keyboard mode.
  inputManager.addButtonHandler(InputManager::HandlerPriority::View, "", "navPush",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          const auto* before = windowManager_.getWidget(DisplaySide::Left);
          openPianoRollForSelectedChannel();
          event.consumed = before != windowManager_.getWidget(DisplaySide::Left);
      });

  control.setButtonActive("step", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "step",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;

          // Step-mode flow across every context:
          //   1. If PatternWidget isn't on Left, open it (exiting keyboard
          //      mode so the Pads/Instruments filter reflects the context)
          //      and activate step mode.
          //   2. If it's already on Left, toggle step mode (on → edit, off
          //      → plain pattern view).
          // Keyboard owns pad input at View priority, so it must be exited
          // before Step mode is toggled even when Pattern is already visible
          // on the left panel.
          if (keyboardModeActive_)
              exitKeyboardMode();

          auto* existing = dynamic_cast<PatternWidget*>(windowManager_.getWidget(DisplaySide::Left));
          if (existing == nullptr)
          {
              windowManager_.dismissAllDialogs();
              auto pw = createPatternWidget();
              auto* raw = pw.get();
              windowManager_.open(std::move(pw), DisplaySide::Left);
              raw->toggleStepMode();
          }
          else
          {
              windowManager_.setFocus(DisplaySide::Left);
              existing->toggleStepMode();
          }
          windowManager_.reclaimResources(DisplaySide::Left);
          event.consumed = true;
      });

  control.setButtonActive("padMode", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "padMode",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;

          // Pad mode and keyboard mode are mutually exclusive — exiting
          // keyboard mode flips PatternWidget back to the Pads channel
          // filter so the sequencer rows reflect the drum-kit world.
          if (keyboardModeActive_)
              exitKeyboardMode();

          // Pad mode and Step mode are mutually exclusive. Merely focusing
          // Pad Overview used to leave PatternWidget's step state active in
          // the background, so Step-only controls such as Euclid remained
          // visible and Step mode could unexpectedly resume later.
          if (auto* pattern = dynamic_cast<PatternWidget*>(
                  windowManager_.getWidget(DisplaySide::Left));
              pattern != nullptr && pattern->isStepModeActive())
          {
              pattern->toggleStepMode();
          }

          auto padOverview = std::make_unique<PadOverviewWidget>();
          padOverview->setPadSelectedCallback([this](int /*padIndex*/) {
              openPadDetailsForSelection();
          });
          windowManager_.open(std::move(padOverview), DisplaySide::Right);
          // Mode-active brightness; dim resumes when another mode takes over.
          control.setButtonBrightness("padMode", HardwareConstants::kLedMedium);
          event.consumed = true;
      });

  // Keyboard mode: "keyboard" hardware button toggles; pads 1..16 become a
  // chromatic MIDI keyboard routed to the currently-selected pad's track.
  // Shift + pad 13/14 shifts the base pitch by a semitone; Shift + pad
  // 15/16 shifts by an octave. A KeyboardWidget opens on the Right panel.
  control.setButtonActive("keyboard", true);
  updateKeyboardLed();
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "keyboard",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          if (keyboardModeActive_)
              exitKeyboardMode();
          else
              enterKeyboardMode();
          event.consumed = true;
      });

  // Fixed Velocity is shared by pad and keyboard modes. Its Shift function
  // enables a 16-step velocity curve for the instrument keyboard: every pad
  // keeps its chromatic pitch while pressure is quantised to sixteen levels.
  control.setButtonActive("fixedVel", true);
  updateFixedVelocityLed();
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "fixedVel",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;

          if (event.isShift())
          {
              sixteenVelocities_ = ! sixteenVelocities_;
              if (sixteenVelocities_)
                  fixedVelocity_ = false;
          }
          else if (sixteenVelocities_)
          {
              sixteenVelocities_ = false;
              fixedVelocity_ = true;
          }
          else
          {
              fixedVelocity_ = ! fixedVelocity_;
          }

          if (keyboardWidgetPtr_ != nullptr)
          {
              keyboardWidgetPtr_->setVelocityMode(
                  fixedVelocity_, sixteenVelocities_);
              const juce::String status = sixteenVelocities_
                  ? "16 Velocities"
                  : (fixedVelocity_ ? "Fixed Velocity 127"
                                    : "Velocity Sensitive");
              windowManager_.showToast(
                  DisplaySide::Right, ToastKind::Info, status);
          }
          updateFixedVelocityLed();
          event.consumed = true;
      });

  // noteRepeatArp: toggles the keyboard arpeggiator on/off. Shift+press
  // opens the ArpDialog for tweaking mode/rate/oct/gate. LED reflects
  // current arp state.
  control.setButtonActive("noteRepeatArp", true);
  updateArpLed();
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "noteRepeatArp",
      [this](InputEvent& event) {
          if (!event.metadata.contains("pressed") || !static_cast<bool>(event.metadata["pressed"])) return;
          const bool shift = event.metadata.contains("shift")
                          && static_cast<bool>(event.metadata["shift"]);
          if (shift)
              showArpDialog();
          else
              toggleArp();
          updateArpLed();
          event.consumed = true;
      });

  // plugin: toggles the PluginEditorWidget on the Left panel for the
  // currently-active keyboard-bank plugin. libmk3 calls the button "plugin"
  // (see external/mk3/mk3_input_map.c) — the "pluginInstance" name used in
  // Mk3Device's LED-reset list is an alias of the same physical button.
  control.setButtonActive("plugin", true);
  control.setButtonBrightness("plugin", HardwareConstants::kLedDim);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "",
      "plugin",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          togglePluginEditor();
          control.setButtonBrightness("plugin",
              pluginEditorOpen_ ? HardwareConstants::kLedMedium
                                : HardwareConstants::kLedDim);
          event.consumed = true;
      });

  // View-priority pad handler: while keyboard mode is active, pad events
  // become note on/off on the selected pad's track (or base-pitch shifts
  // when Shift is held on pads 13..16). Consuming the event here keeps the
  // Global sample-trigger handler from firing.
  inputManager.addPadHandler(
      InputManager::HandlerPriority::View,
      "",
      [this](InputEvent& event) {
          if (!keyboardModeActive_) return;
          const int padIndex = event.padIndex();
          if (padIndex < 0) return;
          const bool pressed = event.isPressed();
          const bool shift = event.isShift();
          if (padIndex >= HardwareConstants::kPadCount)
              return;

          // Shift is a global "tool modifier" for pad-based actions. While
          // shift is held, no pad should play a note — only bound shift+pad
          // actions fire (base-pitch shift on pads 13..16). Unbound
          // shift+pad combos consume silently (no note, no-op).
          const int heldPitch = keyboardHeldPitches_[
              static_cast<size_t>(padIndex)];
          if (shift && pressed)
          {
              event.consumed = true;

              if (padIndex >= 12)
              {
                  const int before = keyboardModeBasePitch_;
                  juce::String stepLabel;
                  switch (padIndex)
                  {
                      case 12: keyboardModeBasePitch_ -= 1;  stepLabel = "Semitone −"; break;
                      case 13: keyboardModeBasePitch_ += 1;  stepLabel = "Semitone +"; break;
                      case 14: keyboardModeBasePitch_ -= 12; stepLabel = "Octave −";   break;
                      case 15: keyboardModeBasePitch_ += 12; stepLabel = "Octave +";   break;
                  }
                  keyboardModeBasePitch_ = juce::jlimit(midi::kNoteMin, midi::kNoteMax - 15, keyboardModeBasePitch_);
                  if (keyboardWidgetPtr_ != nullptr)
                      keyboardWidgetPtr_->setBasePitch(keyboardModeBasePitch_);

                  static const char* kNames[] = {
                      "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                  const int p = keyboardModeBasePitch_;
                  const int oct = (p / 12) - 1;
                  const int name = ((p % 12) + 12) % 12;
                  const juce::String rangeLabel =
                      juce::String(kNames[name]) + juce::String(oct)
                      + " – "
                      + juce::String(kNames[((p + 15) % 12 + 12) % 12])
                      + juce::String(((p + 15) / 12) - 1);
                  const bool clamped = (keyboardModeBasePitch_ == before);
                  windowManager_.showToast(DisplaySide::Right,
                      ToastKind::Info,
                      (clamped ? "Clamped · " : juce::String())
                          + stepLabel + "  (" + rangeLabel + ")");
              }
              return;
          }
          if (shift && ! pressed && heldPitch < 0)
          {
              event.consumed = true;
              return;
          }

          auto& bank = audio.getKeyboardBank();
          auto* track = bank.getTrack();
          if (track == nullptr || bank.isOnEmptySlot())
          {
              if (pressed)
                  juce::Logger::writeToLog(
                      juce::String("[KeyboardMode] no instrument — track=")
                      + (track ? "ok" : "NULL")
                      + " onEmptySlot=" + juce::String((int) bank.isOnEmptySlot())
                      + " numSlots=" + juce::String(bank.getNumSlots()));
              event.consumed = true;
              return;
          }

          const int pitch = pressed
              ? keyboardModeBasePitch_ + padIndex
              : heldPitch;
          if (! pressed && pitch < 0)
          {
              event.consumed = true;
              return;
          }

          int velocity = 100;
          if (pressed && fixedVelocity_)
          {
              velocity = midi::kDefaultFixedVelocity;
          }
          else if (pressed && event.metadata.contains("pressure"))
          {
              const int pressure = event.metadata["pressure"];
              if (pressure > 0)
              {
                  const float norm = juce::jlimit(0.0f, 1.0f,
                      static_cast<float>(pressure)
                          / static_cast<float>(HardwareConstants::kPadPressureMax));
                  // The press edge arrives near the start of the strike,
                  // before pressure reaches its peak. Keep synths with a
                  // strong velocity curve audible without using later
                  // pressure updates as additional note triggers.
                  const int pressureVelocity = juce::jlimit(
                      midi::kVelocityLiveMin, midi::kVelocityMax,
                      juce::roundToInt(std::sqrt(norm) * 127.0f));
                  if (sixteenVelocities_)
                  {
                      velocity = midi::quantizeToSixteenVelocityLevels(
                          pressureVelocity);
                  }
                  else
                  {
                      constexpr int kKeyboardVelocityFloor = 32;
                      velocity = juce::jmax(
                          kKeyboardVelocityFloor, pressureVelocity);
                  }
              }
          }

          if (pressed)
              keyboardHeldPitches_[static_cast<size_t>(padIndex)] = pitch;

          // Route through the keyboard arp when it's on. Off = the arp's
          // noteOn/Off are never emitted (mode==Off short-circuits), so we
          // fall through to direct injection for the default behaviour.
          auto& arp = audio.getKeyboardArp();
          if (arp.isActive())
          {
              arp.setTrack(track);
              if (pressed) arp.noteOn(pitch, velocity);
              else         arp.noteOff(pitch);
          }
          else
          {
              if (pressed)
                  track->injectLiveMidiMessage(
                      juce::MidiMessage::noteOn(1, pitch, static_cast<juce::uint8>(velocity)),
                      {});
              else
                  track->injectLiveMidiMessage(
                      juce::MidiMessage::noteOff(1, pitch),
                      {});
          }

          if (keyboardWidgetPtr_ != nullptr)
              keyboardWidgetPtr_->setPadFlashed(padIndex, pressed, velocity);

          // MIDI recording: write the note into the active pad's MidiClip
          // when recordArmed + transport is playing. Press captures the
          // start beat + pitch + velocity; release computes the length
          // and commits the note. Skipped during count-in (no capture
          // window yet) and when the arp is on (arp-emitted notes are
          // captured separately via the arp's noteEmittedCallback so
          // playback reproduces the arpeggiated output, not just the
          // held root).
          if (audio.isRecordArmed() && audio.isPlaying() && !audio.isCountingIn()
              && !arp.isActive())
          {
              auto* edit = audio.getEdit();
              auto* mc = bank.getMidiClip();

              if (mc != nullptr && edit != nullptr)
              {
                  // Compute current beat within the clip (looping across
                  // clip length so writes stay in bounds).
                  const auto transportPos = edit->getTransport().getPosition();
                  const auto beatsAbs = edit->tempoSequence.toBeats(transportPos);
                  const auto clipStartBeats = edit->tempoSequence.toBeats(
                      mc->getPosition().getStart());
                  const double clipLenBeats =
                      juce::jmax(0.001, mc->getLengthInBeats().inBeats());
                  double beatInClip = (beatsAbs - clipStartBeats).inBeats();
                  beatInClip = std::fmod(beatInClip, clipLenBeats);
                  if (beatInClip < 0) beatInClip += clipLenBeats;

                  auto& pending = keyboardPendingRec_[static_cast<size_t>(padIndex)];
                  if (pressed)
                  {
                      pending.active   = true;
                      pending.startBeat = beatInClip;
                      pending.pitch    = pitch;
                      pending.velocity = velocity;
                  }
                  else if (pending.active)
                  {
                      double length = beatInClip - pending.startBeat;
                      if (length <= 0)
                          length += clipLenBeats;  // wrapped around
                      // Minimum length so zero-hold taps are still audible.
                      length = juce::jmax(length, 0.0625);   // 1/64-note floor

                      mc->getSequence().addNote(
                          pending.pitch,
                          tracktion::BeatPosition::fromBeats(pending.startBeat),
                          tracktion::BeatDuration::fromBeats(length),
                          pending.velocity,
                          /*colour*/ 0,
                          nullptr);

                      pending.active = false;
                  }
              }
          }

          if (! pressed)
              keyboardHeldPitches_[static_cast<size_t>(padIndex)] = -1;

          event.consumed = true;
      });

  control.setButtonActive("browserPlugin", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "browserPlugin",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;

          // If PadDetailsWidget is open, browse for that pad.
          auto* padDetails = dynamic_cast<PadDetailsWidget*>(
              windowManager_.getWidget(DisplaySide::Right));
          if (padDetails == nullptr)
              padDetails = dynamic_cast<PadDetailsWidget*>(
                  windowManager_.getWidget(DisplaySide::Left));

          if (padDetails != nullptr && padDetails->getCurrentPadId() >= 0)
          {
              openBrowserForPad(padDetails->getCurrentPadId());
          }
          else
          {
              if (sampleBrowser_ != nullptr)
                  hideSampleBrowser();
              else
                  showSampleBrowser();
          }
          event.consumed = true;
      });

  // Shift + tapMetro toggles the metronome globally. Plain tapMetro records
  // tap times and updates the global tempo.
  control.setButtonActive("tapMetro", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "tapMetro",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          if (event.isShift())
          {
              const bool wasOn = audio.isClickEnabled();
              audio.enableClick(!wasOn);
              windowManager_.showToast(DisplaySide::Left, ToastKind::Info,
                  !wasOn ? "Metronome ON" : "Metronome OFF", 1500);
              event.consumed = true;
              return;
          }

          const auto now = juce::Time::getMillisecondCounter();
          while (!tapTempoTimes_.empty()
              && now - tapTempoTimes_.front() > 2000)
              tapTempoTimes_.pop_front();
          tapTempoTimes_.push_back(now);

          if (tapTempoTimes_.size() >= 2)
          {
              double intervalMs = 0.0;
              for (size_t i = 1; i < tapTempoTimes_.size(); ++i)
                  intervalMs += static_cast<double>(tapTempoTimes_[i]
                                                    - tapTempoTimes_[i - 1]);
              intervalMs /= static_cast<double>(tapTempoTimes_.size() - 1);

              auto* edit = audio.getEdit();
              if (edit != nullptr && intervalMs > 0.0)
              {
                  const double current = audio.getTransportSnapshot().tempoBpm;
                  const double next = juce::jlimit(20.0, 300.0,
                                                   60000.0 / intervalMs);
                  audio.getUndoManager().beginNewTransaction("Tap Tempo");
                  audio.getUndoManager().perform(
                      new SetTempoCommand(*edit, current, next));
                  windowManager_.showToast(DisplaySide::Left, ToastKind::Info,
                      juce::String(next, 1) + " BPM", 1000);
              }
          }
          event.consumed = true;
      });

  control.setButtonActive("settings", true);
  inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", "settings",
      [this](InputEvent& event) {
          if (! event.isPressed()) return;
          if (windowManager_.hasDialog())
          {
              windowManager_.dismissDialog();
          }
          else
          {
              auto dialog = std::make_unique<SettingsDialog>();
              dialog->setDismissCallback([this]() { windowManager_.dismissDialog(); });
              windowManager_.showDialog(std::move(dialog));
          }
          event.consumed = true;
      });

  // Register pad handler with InputManager at Global priority
  inputManager.addPadHandler(
      InputManager::HandlerPriority::Global,
      "", // Global context
      [this](InputEvent& event)
      {
          const int padIndex = event.padIndex();
          auto& pads = audio.getSampler();
          if (padIndex < 0 || padIndex >= pads.getPadCount())
              return;
          const bool pressed = event.isPressed();

          // In select mode, select the pad and open PadDetailsWidget
          if (control.isSelectPressed())
          {
              if (!pressed)
              {
                  event.consumed = true;
                  return;
              }
              auto snapshots = pads.getPadsSnapshot(
                  SamplerInstrument::SnapshotContent::State);
              if (padIndex >= 0 && padIndex < static_cast<int>(snapshots.size()))
              {
                  pads.selectPad(snapshots[static_cast<size_t>(padIndex)].id);
                  openPadDetailsForSelection();
              }
              event.consumed = true;
              return;
          }

          // In pattern step mode, route pads to step toggling instead of sample trigger
          if (auto* pw = dynamic_cast<PatternWidget*>(windowManager_.getFocusedWidget()))
          {
              if (pw->isStepModeActive())
              {
                  if (!pressed)
                  {
                      event.consumed = true;
                      return;
                  }
                  ControllerHost::PadEvent padEvent;
                  padEvent.pad = static_cast<uint8_t>(padIndex);
                  padEvent.pressed = true;
                  padEvent.pressure = 0;
                  pw->handlePad(padEvent);
                  event.consumed = true;
                  return;
              }
          }

          if (!pressed)
          {
              pads.releasePad(padIndex);
              event.consumed = true;
              return;
          }

          int pressure = HardwareConstants::kPadPressureMax;
          if (event.metadata.contains("pressure"))
              pressure = event.metadata["pressure"];

          float velocity;
          if (fixedVelocity_)
              velocity = 1.0f;
          else if (pressure > 0)
          {
              // A sqrt curve boosts mid-range hits so
              // a firm strike feels like a firm strike.
              const float normalized = juce::jlimit(0.0f, 1.0f,
                  static_cast<float>(pressure)
                      / static_cast<float>(HardwareConstants::kPadPressureMax));
              velocity = juce::jlimit(0.15f, 1.0f, std::sqrt(normalized));
          }
          else
              velocity = 1.0f;

          // Record step if armed + playing + past count-in. During count-in
          // the transport reports playing but we must not capture yet.
          if (audio.isRecordArmed() && audio.isPlaying() && !audio.isCountingIn())
          {
              const int stepCount = pads.getStepCount();
              const double playhead = pads.getPlayheadStep();
              int stepIndex = static_cast<int>(std::round(playhead)) % stepCount;
              if (stepIndex < 0) stepIndex += stepCount;
              const int patternIndex = pads.getActivePatternIndex();
              pads.setStep(patternIndex, padIndex, stepIndex, true);
              pads.setStepVelocity(patternIndex, padIndex, stepIndex,
                                   juce::jlimit(midi::kVelocityLiveMin, midi::kVelocityMax, static_cast<int>(velocity * 127.0f)));
          }

          pads.trigger(padIndex, velocity);
          event.consumed = true;
      });

  control.registerPadSelectionCallback([this](std::optional<int> padIndex)
  {
      juce::MessageManager::callAsync([this, padIndex]()
      {
          if (padIndex.has_value())
          {
              if (auto* editor = dynamic_cast<AudioEditorWidget*>(
                      windowManager_.getWidget(DisplaySide::Left));
                  editor != nullptr && editor->selectManualSlice(*padIndex))
              {
                  windowManager_.setFocus(DisplaySide::Left);
                  return;
              }
          }

          auto& pads = audio.getSampler();

          if (!padIndex.has_value())
          {
              pads.selectPad(-1);
              return;
          }

          const int index = *padIndex;
          auto snapshots = pads.getPadsSnapshot(
              SamplerInstrument::SnapshotContent::State);
          if (index < 0 || index >= static_cast<int>(snapshots.size()))
              return;

          pads.selectPad(snapshots[static_cast<size_t>(index)].id);
          openPadDetailsForSelection();
      });
  });

  control.setControllerInputHandler(&controllerInput);

  // Capture arp output into the keyboard bank's active MidiClip while
  // record is armed. Fires from the arp's timer (message thread).
  audio.getKeyboardArp().setNoteEmittedCallback(
      [this](int pitch, int velocity, double lengthBeats) {
          if (!audio.isRecordArmed() || !audio.isPlaying() || audio.isCountingIn())
              return;

          auto* edit = audio.getEdit();
          auto* mc = audio.getKeyboardBank().getMidiClip();
          if (edit == nullptr || mc == nullptr)
              return;

          const auto beatsAbs = edit->tempoSequence.toBeats(edit->getTransport().getPosition());
          const auto clipStartBeats = edit->tempoSequence.toBeats(mc->getPosition().getStart());
          const double clipLenBeats = juce::jmax(0.001, mc->getLengthInBeats().inBeats());
          double beatInClip = (beatsAbs - clipStartBeats).inBeats();
          beatInClip = std::fmod(beatInClip, clipLenBeats);
          if (beatInClip < 0) beatInClip += clipLenBeats;

          mc->getSequence().addNote(
              pitch,
              tracktion::BeatPosition::fromBeats(beatInClip),
              tracktion::BeatDuration::fromBeats(juce::jmax(1.0 / 64.0, lengthBeats)),
              juce::jlimit(midi::kVelocityLiveMin, midi::kVelocityMax, velocity),
              /*colour*/ 0,
              nullptr);
      });

  // Default bindings for transport control
  auto bindTransportActions = [this]() {
      const auto playAction = [this](InputEvent&) {
          audio.play();
      };

      const auto stopAction = [this](InputEvent&) {
          audio.stop();
      };

      const auto recordToggleAction = [this](InputEvent&) {
          audio.toggleRecord();
      };

      const auto loopToggleAction = [this](InputEvent&) {
          audio.toggleLoop();
      };

      const auto toggleTransport = [this, playAction, stopAction](InputEvent& event) {
          if (auto* edit = audio.getEdit())
          {
              auto& transport = edit->getTransport();
              if (transport.isPlaying())
                  stopAction(event);
              else
                  playAction(event);
          }
          else
          {
              playAction(event);
          }
      };

      inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                                 juce::KeyPress(juce::KeyPress::spaceKey), toggleTransport);

      inputManager.addMidiHandler(InputManager::HandlerPriority::Global, "", playAction,
                                  1, 60, {});
      inputManager.addMidiHandler(InputManager::HandlerPriority::Global, "", stopAction,
                                  1, 61, {});
      inputManager.addMidiHandler(InputManager::HandlerPriority::Global, "", loopToggleAction,
                                  1, 20, {});

      inputManager.addControllerHandler(InputManager::HandlerPriority::Global, "",
                                        "transport.play", playAction);
      inputManager.addControllerHandler(InputManager::HandlerPriority::Global, "",
                                        "transport.stop", stopAction);
      inputManager.addControllerHandler(InputManager::HandlerPriority::Global, "",
                                        "transport.record", recordToggleAction);
      inputManager.addControllerHandler(InputManager::HandlerPriority::Global, "",
                                        "transport.loop.toggle", loopToggleAction);
      inputManager.addControllerHandler(InputManager::HandlerPriority::Global, "",
                                        "transport.toggle", toggleTransport);
  };

  bindTransportActions();

  // Numpad and top-row number keys simulate pad input
  auto bindPadShortcuts = [this]() {
      const std::array<int, 9> numpadKeyCodes{
          juce::KeyPress::numberPad1,
          juce::KeyPress::numberPad2,
          juce::KeyPress::numberPad3,
          juce::KeyPress::numberPad4,
          juce::KeyPress::numberPad5,
          juce::KeyPress::numberPad6,
          juce::KeyPress::numberPad7,
          juce::KeyPress::numberPad8,
          juce::KeyPress::numberPad9
      };

      for (int padIndex = 0; padIndex < static_cast<int>(numpadKeyCodes.size()); ++padIndex)
      {
          const int digitKeyCode = static_cast<int>('1') + padIndex;
          const juce::KeyPress topRowKey(digitKeyCode, juce::ModifierKeys(), 0);
          const juce::KeyPress numpadKey(numpadKeyCodes[static_cast<size_t>(padIndex)],
                                         juce::ModifierKeys(), 0);

          const auto bindPad = [this, padIndex](const juce::KeyPress& key)
          {
              inputManager.addKeyHandler(
                  InputManager::HandlerPriority::Global,
                  "",
                  key,
                  [this, padIndex](InputEvent&) {
                      inputManager.dispatchPad(
                          static_cast<uint8_t>(padIndex),
                          true,
                          16383,
                          keyboardShiftActive,
                          "keyboard"
                      );
                  });
          };

          bindPad(topRowKey);
          bindPad(numpadKey);
      }
  };

  bindPadShortcuts();

  // Global undo/redo: (Macro OR Shift) + Pad 1 (undo) / Pad 2 (redo).
  // Either modifier works — Macro matches the Maschine-native convention; Shift
  // mirrors keyboard Ctrl+Z behaviour on the pad grid.
  inputManager.addPadHandler(
      InputManager::HandlerPriority::Global,
      "",
      [this](InputEvent& event)
      {
          if (!event.metadata.contains("pressed") || !event.metadata.contains("pad") || !event.metadata.contains("macro"))
              return;

          const bool pressed = event.metadata["pressed"];
          const bool macro = event.metadata["macro"];
          const bool shift = event.isShift();
          const int padIndex = event.metadata["pad"];

          if (!pressed || !(macro || shift))
              return;

          const juce::String sourceModifier = macro ? "macro" : "shift";
          if (padIndex == 0)
          {
              performUndo("controller " + sourceModifier + "+pad 1");
              event.consumed = true;
          }
          else if (padIndex == 1)
          {
              performRedo("controller " + sourceModifier + "+pad 2");
              event.consumed = true;
          }
      });

  const juce::KeyPress undoKey('z', juce::ModifierKeys::ctrlModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             undoKey,
                             [this](InputEvent&) { performUndo("Ctrl+Z"); });

  const juce::KeyPress redoKey('z',
                               juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             redoKey,
                             [this](InputEvent&) { performRedo("Ctrl+Alt+Z"); });

  const juce::KeyPress mixerToggleKey('m', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             mixerToggleKey,
                             [openMixerPair](InputEvent&) {
                                 openMixerPair(std::nullopt, std::nullopt);
                             });

  const juce::KeyPress samplingToggleKey('s', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             samplingToggleKey,
                             [this](InputEvent&) { openSamplingWorkflow(); });

  const juce::KeyPress patternToggleKey('p', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             patternToggleKey,
                             [this](InputEvent&) {
                                 windowManager_.open(createPatternWidget(), DisplaySide::Left);
                   
                             });

  const juce::KeyPress recordKey('r', juce::ModifierKeys::noModifiers, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             recordKey,
                             [this](InputEvent&) { audio.toggleRecord(); });

  const juce::KeyPress audioSettingsKey(',', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             audioSettingsKey,
                             [this](InputEvent&) { showAudioSettingsDialog(); });

  const juce::KeyPress arrangerKey('a', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             arrangerKey,
                             [this](InputEvent&) {
                                 windowManager_.open(std::make_unique<ArrangerWidget>(), DisplaySide::Left);

                             });

  const juce::KeyPress fileKey('f', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             fileKey,
                             [this](InputEvent&) {
                                 auto* left = windowManager_.getWidget(DisplaySide::Left);
                                 const bool open = (left != nullptr && left->describe().id == "file_browser");
                                 if (open) dismissFileDialog();
                                 else      showFileDialog(FileBrowserWidget::Mode::Browse);
                             });

  // Ctrl+Alt+O -- Pad Mode toggle
  const juce::KeyPress padModeKey('o', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             padModeKey,
                             [this](InputEvent&) {
                                 auto padOverview = std::make_unique<PadOverviewWidget>();
                                 windowManager_.open(std::move(padOverview), DisplaySide::Right);
                             });

  // L -- Loop toggle
  const juce::KeyPress loopKey('l', juce::ModifierKeys::noModifiers, 0);
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             loopKey,
                             [this](InputEvent&) { audio.toggleLoop(); });

  // Arrow keys -- nav buttons
  auto bindNavKey = [this](int keyCode, const std::string& navButton)
  {
      inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                                 juce::KeyPress(keyCode),
                                 [this, navButton](InputEvent&)
                                 {
                                     ControllerHost::ButtonEvent evt;
                                     evt.name = navButton;
                                     evt.pressed = true;
                                     evt.shift = keyboardShiftActive;
                                     control.handleButtonEvent(evt);
                                 });
  };

  bindNavKey(juce::KeyPress::upKey, "navUp");
  bindNavKey(juce::KeyPress::downKey, "navDown");
  bindNavKey(juce::KeyPress::leftKey, "navLeft");
  bindNavKey(juce::KeyPress::rightKey, "navRight");
  bindNavKey(juce::KeyPress::returnKey, "navPush");

  // [ -- arrowLeft, ] -- arrowRight
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             juce::KeyPress('['),
                             [this](InputEvent&)
                             {
                                 ControllerHost::ButtonEvent evt;
                                 evt.name = "arrowLeft";
                                 evt.pressed = true;
                                 evt.shift = keyboardShiftActive;
                                 control.handleButtonEvent(evt);
                             });
  inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                             juce::KeyPress(']'),
                             [this](InputEvent&)
                             {
                                 ControllerHost::ButtonEvent evt;
                                 evt.name = "arrowRight";
                                 evt.pressed = true;
                                 evt.shift = keyboardShiftActive;
                                 control.handleButtonEvent(evt);
                             });

  // F1-F8 -- Groups g1-g8
  for (int i = 0; i < 8; ++i)
  {
      const int fKeyCode = juce::KeyPress::F1Key + i;
      const std::string groupButton = "g" + std::to_string(i + 1);
      inputManager.addKeyHandler(InputManager::HandlerPriority::Global, "",
                                 juce::KeyPress(fKeyCode),
                                 [this, groupButton](InputEvent&)
                                 {
                                     ControllerHost::ButtonEvent evt;
                                     evt.name = groupButton;
                                     evt.pressed = true;
                                     evt.shift = keyboardShiftActive;
                                     control.handleButtonEvent(evt);
                                 });
  }

  // D-button handler for the remaining legacy ScreenView dialog. Widget
  // options (including the sample browser) are routed by WindowManager.
  for (int i = 1; i <= 8; ++i)
  {
      const std::string dName = "d" + std::to_string(i);
      inputManager.addButtonHandler(InputManager::HandlerPriority::Global, "", dName,
          [this, i](InputEvent& event) {
              if (! event.isPressed()) return;

              ScreenView* overlay = nullptr;
              if (optionDialog != nullptr && optionDialog->isVisible())
                  overlay = optionDialog.get();

              if (overlay != nullptr)
              {
                  // d1-d4 for left-panel overlays (slot index 0-3)
                  int slot = (i - 1) % 4;
                  overlay->triggerOption(static_cast<size_t>(slot));
                  event.consumed = true;
              }
          });
  }

  // Reset all controller LEDs to ensure clean startup state
  control.resetAllLeds();

  // Open default views: Pattern on left, PadOverview (pad mode) on right
  windowManager_.open(createPatternWidget(), DisplaySide::Left);
  {
      auto padOverview = std::make_unique<PadOverviewWidget>();
      padOverview->setPadSelectedCallback([this](int /*padIndex*/) {
          openPadDetailsForSelection();
      });
      windowManager_.open(std::move(padOverview), DisplaySide::Right);
  }
  windowManager_.setFocus(DisplaySide::Right);

  // Re-assert button LEDs after resetAllLeds wiped them. setButtonActive
  // lights each at kLedDim (available). Mode-active brightness
  // (kLedMedium) is set separately by the relevant handlers; pressed
  // brightness (kLedBright) is handled automatically by Mk3Controller.
  control.beginLedBatch();
  control.setButtonActive("select", true);
  control.setButtonActive("padMode", true);
  control.setButtonActive("step", true);
  control.setButtonActive("pattern", true);
  control.setButtonActive("mixer", true);
  control.setButtonActive("sampling", true);
  control.setButtonActive("arranger", true);
  control.setButtonActive("fileSave", true);
  control.setButtonActive("settings", true);
  control.setButtonActive("browserPlugin", true);
  control.setButtonActive("recCountIn", true);
  control.setButtonActive("keyboard", true);
  control.setButtonActive("fixedVel", true);
  control.setButtonActive("plugin", true);
  control.setButtonActive("tapMetro", true);
  control.endLedBatch();

  midiInput.start();
  keyboardInput.start();

  startTimerHz(60);

  setWantsKeyboardFocus(true);
  grabKeyboardFocus();
}

UiHost::~UiHost()
{
  audio.removeListener(this);

  // 1. Stop the display/bar refresh timer first. After this point no more
  //    display frames or LED writes originate from the message thread.
  stopTimer();

  // 2. Detach input sources so nothing can post events into a dying host.
  keyboardInput.stop();
  midiInput.stop();

  // 3. Clear the MK3 displays to black — visible shutdown signal, bounded
  //    by libusb's two bulk-transfer timeouts (~2 s worst case). LEDs stay in
  //    their last-drawn state; shutdown itself remains limited to closing the
  //    controller threads and shared handle.
  control.clearAllDisplays();

  // 4. Shut down controllers: joins the input poll thread and closes the
  //    libusb handle. After this the device pointer is null.
  control.shutdownControllers();

  setLookAndFeel(nullptr);
}

void UiHost::editAboutToBeReplaced()
{
  windowManager_.notifyEditAboutToBeReplaced();
  notifyStashedWidgets(true);
}

void UiHost::editReplaced()
{
  windowManager_.notifyEditReplaced();
  notifyStashedWidgets(false);
}

void UiHost::activeSamplerAboutToChange()
{
  windowManager_.notifyActiveSamplerAboutToChange();
}

void UiHost::activeSamplerChanged()
{
  windowManager_.notifyActiveSamplerChanged();
}

void UiHost::notifyStashedWidgets(bool beforeReplacement)
{
  Widget* const stashedWidgets[] = {
      keyboardStashRight_.get(),
      pluginEditorStashLeft_.get(),
      pianoRollStashLeft_.get(),
      stashedLeft_.get(),
      stashedRight_.get(),
      pluginBrowserStashLeft_.get(),
      pluginBrowserStashRight_.get(),
      audioRecorderStashLeft_.get(),
      audioRecorderStashRight_.get(),
      sliceDetailsStashRight_.get(),
      sampleBrowserStashLeft_.get()
  };
  for (auto* widget : stashedWidgets)
      if (widget != nullptr)
      {
          if (beforeReplacement)
              widget->onEditAboutToBeReplaced();
          else
              widget->onEditReplaced();
      }
}

void UiHost::resized()
{
  windowManager_.setBounds(0, 0, 960, 272);
}

bool UiHost::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::tabKey)
    {
        updateKeyboardSelectState(true);
        return true;
    }

    return juce::Component::keyPressed(key);
}

bool UiHost::keyStateChanged(bool isKeyDown)
{
    juce::ignoreUnused(isKeyDown);
    const bool shiftHeld = juce::ModifierKeys::currentModifiers.isShiftDown();
    updateKeyboardShiftState(shiftHeld);

    const bool tabHeld = juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::tabKey);
    updateKeyboardSelectState(tabHeld);
    return false;
}

void UiHost::requestDisplayRefresh() noexcept
{
    requestDisplayRefresh(3u);
}

void UiHost::requestDisplayRefresh(unsigned panelMask) noexcept
{
    displayDirtyMask_.fetch_or(panelMask & 3u, std::memory_order_release);
}

void UiHost::timerCallback()
{
    const double callbackStarted = performanceLoggingEnabled_
        ? juce::Time::getMillisecondCounterHiRes() : 0.0;
    double updateFinished = callbackStarted;
    double paintFinished = callbackStarted;
    bool renderedFrame = false;

    const auto recordPerformance = [&]
    {
        if (!performanceLoggingEnabled_)
            return;

        const double callbackFinished = juce::Time::getMillisecondCounterHiRes();
        const double callbackMs = callbackFinished - callbackStarted;
        performanceWindow_.callbackMs += callbackMs;
        performanceWindow_.updateMs += updateFinished - callbackStarted;
        performanceWindow_.paintMs += paintFinished - updateFinished;
        performanceWindow_.submitMs += callbackFinished - paintFinished;
        performanceWindow_.maxCallbackMs = juce::jmax(
            performanceWindow_.maxCallbackMs, callbackMs);
        ++performanceWindow_.ticks;
        if (renderedFrame)
            ++performanceWindow_.frames;

        const double windowMs = callbackFinished - performanceWindow_.startedMs;
        if (windowMs < 5000.0)
            return;

        const double ticks = static_cast<double>(performanceWindow_.ticks);
        const double frames = static_cast<double>(performanceWindow_.frames);
        juce::Logger::writeToLog(
            "[UI PERF] tick=" + juce::String(ticks * 1000.0 / windowMs, 1)
            + "Hz frame=" + juce::String(frames * 1000.0 / windowMs, 1)
            + "fps callback=" + juce::String(performanceWindow_.callbackMs / ticks, 3)
            + "ms update=" + juce::String(performanceWindow_.updateMs / ticks, 3)
            + "ms bars=" + juce::String(performanceWindow_.barRefreshMs / ticks, 3)
            + "ms widgets=" + juce::String(performanceWindow_.widgetTickMs / ticks, 3)
            + "ms hw=" + juce::String(performanceWindow_.hardwareFlushMs / ticks, 3)
            + "ms mode=" + juce::String(performanceWindow_.modeLedMs / ticks, 3)
            + "ms paint=" + juce::String(performanceWindow_.paintMs / ticks, 3)
            + "ms submit=" + juce::String(performanceWindow_.submitMs / ticks, 3)
            + "ms max=" + juce::String(performanceWindow_.maxCallbackMs, 3) + "ms");
        performanceWindow_ = {};
        performanceWindow_.startedMs = callbackFinished;
    };

    // Central 60Hz heartbeat for hardware sync: bars/widget state → LED
    // batch flush → display frame push, in that order. The design is
    // deliberate — a single message-thread tick keeps animation state,
    // pad-LED output, and MK3 framebuffer push phase-locked to the same
    // 16ms window. Per-widget juce::Timer instances would break that
    // ordering, and juce::AsyncUpdater / VBlankAttachment don't model
    // headless USB framebuffers. See arch notes in CLAUDE.md if curious.
    windowManager_.refreshBars();
    const double barsFinished = performanceLoggingEnabled_
        ? juce::Time::getMillisecondCounterHiRes() : 0.0;
    windowManager_.tickActiveWidgets();
    const double widgetsFinished = performanceLoggingEnabled_
        ? juce::Time::getMillisecondCounterHiRes() : 0.0;

    // Batched LED flush — coalesces pad/button/knob LED state changes into
    // a single sweep of USB writes per 60 Hz tick instead of one USB transfer
    // per individual setIndexedLed call. Record/play/stop/loop LED rendering
    // is owned by TransportWidget::updateLeds (registered as a system widget,
    // ticked via WindowManager::tickActiveWidgets above) and rides this flush.
    windowManager_.flushHardwareState();
    const double hardwareFinished = performanceLoggingEnabled_
        ? juce::Time::getMillisecondCounterHiRes() : 0.0;
    // Mode buttons are global indicators rather than widget-owned resources.
    // Apply them after the widget-owned hardware flush so a stale resource
    // value from a closing widget cannot overwrite the current mode state.
    updateModeLeds();
    updateFinished = performanceLoggingEnabled_
        ? juce::Time::getMillisecondCounterHiRes() : 0.0;
    if (performanceLoggingEnabled_)
    {
        performanceWindow_.barRefreshMs += barsFinished - callbackStarted;
        performanceWindow_.widgetTickMs += widgetsFinished - barsFinished;
        performanceWindow_.hardwareFlushMs += hardwareFinished - widgetsFinished;
        performanceWindow_.modeLedMs += updateFinished - hardwareFinished;
    }
    paintFinished = updateFinished;

    // Skip the expensive frame paint + USB push when nothing has changed.
    const unsigned dirtyPanels = displayDirtyMask_.exchange(0u, std::memory_order_acq_rel);
    if (dirtyPanels == 0u)
    {
        recordPerformance();
        return;
    }
    renderedFrame = true;

    // Reuse frame buffer to avoid 1MB allocation per tick
    if (!frameBuffer_.isValid() || frameBuffer_.getWidth() != UiTheme::kTotalWidth
        || frameBuffer_.getHeight() != UiTheme::kPanelHeight)
    {
        frameBuffer_ = juce::Image(juce::Image::ARGB,
                                    UiTheme::kTotalWidth,
                                    UiTheme::kPanelHeight,
                                    true,
                                    juce::SoftwareImageType());
    }
    const auto leftBounds = juce::Rectangle<int>(0, 0,
                                                 UiTheme::kPanelWidth,
                                                 UiTheme::kPanelHeight);
    const auto rightBounds = leftBounds.withX(UiTheme::kPanelWidth);

    {
        juce::Graphics g(frameBuffer_);
        juce::RectangleList<int> dirtyClip;
        if ((dirtyPanels & 1u) != 0u)
            dirtyClip.add(leftBounds);
        if ((dirtyPanels & 2u) != 0u)
            dirtyClip.add(rightBounds);
        g.reduceClipRegion(dirtyClip);
        g.fillAll(juce::Colours::transparentBlack);
        paintEntireComponent(g, false);
    }
    paintFinished = performanceLoggingEnabled_
        ? juce::Time::getMillisecondCounterHiRes() : 0.0;

    if ((dirtyPanels & 1u) != 0u)
        control.pushDisplayFrame(0, frameBuffer_.getClippedImage(leftBounds));
    if ((dirtyPanels & 2u) != 0u)
        control.pushDisplayFrame(1, frameBuffer_.getClippedImage(rightBounds));
    recordPerformance();
}

void UiHost::ensureSampleIndexer()
{
    if (sampleIndexer_ != nullptr)
        return;
    sampleIndexer_ = std::make_unique<SampleIndexer>(
        DataPaths::getSamplesDir(),
        DataPaths::getRoot().getChildFile("samples-index.json"),
        SampleTaxonomy::findDefaultFile());
    juce::Component::SafePointer<UiHost> safeThis(this);
    sampleIndexer_->setChangeCallback([safeThis]() mutable
    {
        if (safeThis == nullptr || safeThis->sampleIndexer_ == nullptr)
            return;
        if (safeThis->sampleBrowser_ != nullptr)
        {
            safeThis->sampleBrowser_->setSampleIndex(
                safeThis->sampleIndexer_->snapshot());
            safeThis->sampleBrowser_->setScanning(
                safeThis->sampleIndexer_->isScanning());
        }
        safeThis->requestDisplayRefresh();
    });
}

void UiHost::showSampleBrowser()
{
    hideOptionDialog();
    ensureSampleIndexer();

    if (sampleBrowser_ != nullptr)
        return;

    sampleBrowserStashLeft_ = windowManager_.takeWidget(DisplaySide::Left);

    auto browser = std::make_unique<SampleBrowserComponent>();
    sampleBrowser_ = browser.get();
    sampleBrowser_->setBrowserTitle("Sample Browser");
    sampleBrowser_->setSelectLabels("Load", "Open");
    sampleBrowser_->setPathDisplayName("samples");
    sampleBrowser_->setDefaultRootDirectory(DataPaths::getSamplesDir());
    sampleBrowser_->setEmptyMessage("No samples in this directory");
    sampleBrowser_->setOnBrowseModeChanged([this](SampleBrowserComponent::BrowseMode mode)
    {
        audio.getUiState().setProperty(
            "sampleBrowseMode",
            mode == SampleBrowserComponent::BrowseMode::Categories
                ? "categories" : "folders",
            nullptr);
    });
    sampleBrowser_->setOnRescan([this]()
    {
        if (sampleIndexer_ != nullptr)
            sampleIndexer_->requestScan(true);
    });

    juce::Component::SafePointer<UiHost> safeThis(this);
    sampleBrowser_->setOnFileChosen([safeThis](const juce::File& file)
    {
        juce::MessageManager::callAsync([safeThis, file]() mutable
        {
            if (safeThis == nullptr)
                return;
            safeThis->hideSampleBrowser();
            auto& sampler = safeThis->audio.getSampler();
            int pad = sampler.getSelectedPad();
            if (pad < 0) pad = 0;
            auto& undo = safeThis->audio.getUndoManager();
            undo.beginNewTransaction("Load Sample");
            undo.perform(new LoadSampleCommand(safeThis->audio, pad, file));
            safeThis->windowManager_.showToast(DisplaySide::Left, ToastKind::Success,
                "Loaded: " + file.getFileNameWithoutExtension());
        });
    });
    sampleBrowser_->setOnCancel([safeThis]() mutable
    {
        juce::MessageManager::callAsync([safeThis]() mutable
        {
            if (safeThis != nullptr)
                safeThis->hideSampleBrowser();
        });
    });
    sampleBrowser_->setOnPreview([safeThis](const juce::File& file)
    {
        // Hardware input is marshalled by ControllerGestureProcessor and UI
        // input originates in JUCE callbacks, so preview requests already run
        // on the message thread. Avoid a second async hop that could lag behind
        // rapid navigation or reorder teardown.
        JUCE_ASSERT_MESSAGE_THREAD;
        if (safeThis == nullptr)
            return;
        if (!file.existsAsFile())
        {
            if (safeThis->previewPlayer_ != nullptr)
                safeThis->previewPlayer_->stop();
            return;
        }
        if (safeThis->previewPlayer_ == nullptr)
            safeThis->previewPlayer_ =
                std::make_unique<SamplePreviewPlayer>(safeThis->audio);
        safeThis->previewPlayer_->play(
            safeThis->audio.resolveSampleFileForPlayback(file));
    });

    sampleBrowser_->setRootDirectory(DataPaths::getSamplesDir());
    const auto currentStoredMode = audio.getUiState()
                                       .getProperty("sampleBrowseMode", "folders")
                                       .toString();
    sampleBrowser_->setBrowseMode(
        currentStoredMode == "categories" ? SampleBrowserComponent::BrowseMode::Categories
                                           : SampleBrowserComponent::BrowseMode::Folders);
    sampleBrowser_->setSampleIndex(sampleIndexer_->snapshot());
    sampleBrowser_->setScanning(sampleIndexer_->isScanning());
    windowManager_.open(std::move(browser), DisplaySide::Left);
    windowManager_.setFocus(DisplaySide::Left);
}

void UiHost::hideSampleBrowser()
{
    if (sampleBrowser_ == nullptr)
        return;

    sampleBrowser_ = nullptr;
    windowManager_.close("sample_browser");

    if (previewPlayer_ != nullptr)
        previewPlayer_->stop();

    if (sampleBrowserStashLeft_ != nullptr)
        windowManager_.open(std::move(sampleBrowserStashLeft_), DisplaySide::Left);
    grabKeyboardFocus();
}

void UiHost::showOptionDialog(const juce::String& title,
                              const juce::String& message,
                              const std::vector<OptionDialogComponent::Button>& buttons)
{
    if (optionDialog == nullptr)
    {
        optionDialog = std::make_unique<OptionDialogComponent>();
        addAndMakeVisible(optionDialog.get());
    }

    optionDialog->setTitle(title);
    optionDialog->setMessage(message);
    optionDialog->setButtons(buttons);

    auto bounds = getLocalBounds();
    bounds.setWidth(bounds.getWidth() / 2);
    optionDialog->setBounds(bounds);
    optionDialog->setVisible(true);
    optionDialog->toFront(true);
    optionDialog->grabKeyboardFocus();
}

void UiHost::hideOptionDialog()
{
    if (optionDialog != nullptr)
        optionDialog->setVisible(false);

    grabKeyboardFocus();
}

void UiHost::showAudioSettingsDialog()
{
  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = "Audio Device Settings";
  options.dialogBackgroundColour = juce::LookAndFeel::getDefaultLookAndFeel()
      .findColour(juce::ResizableWindow::backgroundColourId);

  auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
      audio.getAudioDeviceManager(),
      0, 8,
      1, 8,
      true,
      true,
      false,
      false);
  selector->setSize(480, 520);

  options.useNativeTitleBar = true;
  options.resizable = true;
  options.content.setOwned(selector.release());
  options.launchAsync();
}

namespace
{
// Transaction descriptions whose undo/redo should require the user to
// confirm. Anything potentially destructive or hard to re-do manually (a
// sample load, a slice, a truncate) goes here; cheap toggles (step toggle,
// gain tweak, tempo nudge) stay frictionless.
bool undoRequiresConfirmation(const juce::String& description)
{
    static const std::initializer_list<const char*> kNames = {
        "Load Sample",
        "Slice Sample",
        "Normalize Sample",
    };
    for (const auto* name : kNames)
        if (description == name)
            return true;
    return false;
}
} // namespace

void UiHost::performUndo(const juce::String& source)
{
    // Controller/MIDI callbacks land on background threads. Undo mutates
    // JUCE/TE plugin state (SamplerPlugin::removeSound, etc.) which must run
    // on the message thread — otherwise the caller deadlocks against the
    // audio/message pump.
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        juce::MessageManager::callAsync([this, source]() { performUndo(source); });
        return;
    }

    // Global pad gestures are dispatched before modal widget routing. Ignore
    // repeats while the first confirmation is already in control.
    if (windowManager_.hasDialog())
    {
        juce::Logger::writeToLog(
            "[UiHost] Undo ignored while a confirmation dialog is active.");
        return;
    }

    if (!audio.canUndo())
    {
        juce::Logger::writeToLog("[UiHost] Undo requested from " + source + " but nothing to undo.");
        windowManager_.showToast(windowManager_.getFocus(), ToastKind::Info, "Nothing to undo");
        return;
    }

    // Capture the description BEFORE the undo — the stack shifts after.
    const juce::String description = audio.getEdit() != nullptr
                                        ? audio.getEdit()->getUndoManager().getUndoDescription()
                                        : juce::String();
    juce::Logger::writeToLog("[UiHost] Undo requested from " + source);

    // Body is always deferred to the message thread. Without this, the
    // ConfirmDialog's d4 button press (which arrives on the USB thread)
    // would invoke audio.undo() off-thread — SamplerPlugin::removeSound()
    // takes a critical section that can deadlock against the audio thread.
    auto runUndo = [this, description, source]()
    {
        juce::MessageManager::callAsync([this, description, source]()
        {
            if (!audio.undo())
            {
                juce::Logger::writeToLog("[UiHost] Undo failed for source " + source);
                windowManager_.showToast(windowManager_.getFocus(), ToastKind::Error, "Undo failed");
                return;
            }
            const juce::String label = description.isNotEmpty()
                                           ? "Undo: " + description
                                           : juce::String("Undo");
            windowManager_.showToast(windowManager_.getFocus(), ToastKind::Success, label);
        });
    };

    if (undoRequiresConfirmation(description))
    {
        auto dialog = std::make_unique<ConfirmDialog>(
            juce::String("Undo: ") + description,
            juce::String("Undo last \"") + description + "\"?",
            juce::String("Undo"),
            std::move(runUndo));
        dialog->setPreferredSide(windowManager_.getFocus());
        windowManager_.showDialog(std::move(dialog));
        return;
    }

    runUndo();
}

void UiHost::performRedo(const juce::String& source)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        juce::MessageManager::callAsync([this, source]() { performRedo(source); });
        return;
    }

    if (windowManager_.hasDialog())
    {
        juce::Logger::writeToLog(
            "[UiHost] Redo ignored while a confirmation dialog is active.");
        return;
    }

    if (!audio.canRedo())
    {
        juce::Logger::writeToLog("[UiHost] Redo requested from " + source + " but nothing to redo.");
        windowManager_.showToast(windowManager_.getFocus(), ToastKind::Info, "Nothing to redo");
        return;
    }

    const juce::String description = audio.getEdit() != nullptr
                                        ? audio.getEdit()->getUndoManager().getRedoDescription()
                                        : juce::String();
    juce::Logger::writeToLog("[UiHost] Redo requested from " + source);

    auto runRedo = [this, description, source]()
    {
        juce::MessageManager::callAsync([this, description, source]()
        {
            if (!audio.redo())
            {
                juce::Logger::writeToLog("[UiHost] Redo failed for source " + source);
                windowManager_.showToast(windowManager_.getFocus(), ToastKind::Error, "Redo failed");
                return;
            }
            const juce::String label = description.isNotEmpty()
                                           ? "Redo: " + description
                                           : juce::String("Redo");
            windowManager_.showToast(windowManager_.getFocus(), ToastKind::Success, label);
        });
    };

    if (undoRequiresConfirmation(description))
    {
        auto dialog = std::make_unique<ConfirmDialog>(
            juce::String("Redo: ") + description,
            juce::String("Redo \"") + description + "\"?",
            juce::String("Redo"),
            std::move(runRedo));
        dialog->setPreferredSide(windowManager_.getFocus());
        windowManager_.showDialog(std::move(dialog));
        return;
    }

    runRedo();
}

void UiHost::updateKeyboardShiftState(bool active)
{
    if (keyboardShiftActive == active)
        return;

    keyboardShiftActive = active;

    ControllerHost::ButtonEvent event;
    event.name = "shift";
    event.pressed = active;
    event.shift = false;
    control.handleButtonEvent(event);
}

void UiHost::updateKeyboardSelectState(bool active)
{
    if (keyboardSelectActive == active)
        return;

    keyboardSelectActive = active;

    ControllerHost::ButtonEvent event;
    event.name = "select";
    event.pressed = active;
    event.shift = keyboardShiftActive;
    control.handleButtonEvent(event);
}

std::unique_ptr<PadDetailsWidget> UiHost::createPadDetailsWidget()
{
    auto details = std::make_unique<PadDetailsWidget>();
    details->setBackCallback([this]() {
        windowManager_.close("pad_details");
    });
    details->setBrowseCallback([this](int padId) {
        openBrowserForPad(padId);
    });
    details->setBrowseLayerCallback([this](int padId) {
        openBrowserForPad(padId, true);
    });
    details->setOnSetInstrumentRequested([this](int padIndex) {
        showPluginBrowser(PluginBrowserWidget::Filter::Instruments,
            [this, padIndex](const juce::PluginDescription& desc) {
                audio.getUndoManager().beginNewTransaction();
                audio.getUndoManager().perform(
                    new SetPadInstrumentCommand(audio, padIndex, desc));
            });
    });
    details->setOnClearInstrumentRequested([this](int padIndex) {
        audio.getUndoManager().beginNewTransaction();
        audio.getUndoManager().perform(
            new ClearPadInstrumentCommand(audio, padIndex));
    });
    return details;
}

void UiHost::openPadDetailsForSelection()
{
    // Keep PadOverview alive when it is visible: it owns the pad LED resource
    // set and continuously publishes the populated-pad colours. Opening
    // details over that panel releases the ownership and turns every pad off
    // until Pad Mode is opened again.
    if (dynamic_cast<PadOverviewWidget*>(
            windowManager_.getWidget(DisplaySide::Right)) != nullptr)
    {
        windowManager_.open(createPadDetailsWidget(), DisplaySide::Left);
        return;
    }

    if (dynamic_cast<PadOverviewWidget*>(
            windowManager_.getWidget(DisplaySide::Left)) != nullptr)
    {
        windowManager_.open(createPadDetailsWidget(), DisplaySide::Right);
        return;
    }

    windowManager_.open(createPadDetailsWidget(), DisplaySide::Right);
}

std::unique_ptr<AudioEditorWidget> UiHost::createAudioEditorWidget()
{
    auto editor = std::make_unique<AudioEditorWidget>();
    auto* rawEditor = editor.get();
    editor->setSlicesAppliedCallback([this](int sliceCount)
    {
        juce::Component::SafePointer<UiHost> safeThis(this);
        juce::MessageManager::callAsync([safeThis, sliceCount]()
        {
            if (safeThis != nullptr)
                safeThis->openSlicedPadWorkflow(sliceCount);
        });
    });
    editor->setSliceDetailsVisibilityCallback(
        [this, safeEditor = juce::Component::SafePointer<AudioEditorWidget>(rawEditor)]
        (bool visible)
        {
            if (visible)
            {
                if (safeEditor != nullptr)
                    showSliceDetails(*safeEditor);
            }
            else
            {
                hideSliceDetails();
            }
        });
    return editor;
}

void UiHost::openSamplingWorkflow()
{
    windowManager_.open(createAudioEditorWidget(), DisplaySide::Left);
    windowManager_.open(std::make_unique<PadOverviewWidget>(), DisplaySide::Right);
    windowManager_.setFocus(DisplaySide::Left);
}

void UiHost::showAudioRecorder()
{
    if (auto* current = windowManager_.getWidget(DisplaySide::Left);
        current != nullptr && current->describe().id == "audio_recorder")
        return;

    audioRecorderStashLeft_ = windowManager_.takeWidget(DisplaySide::Left);
    audioRecorderStashRight_ = windowManager_.takeWidget(DisplaySide::Right);

    auto recorder = std::make_unique<AudioRecorderWidget>();
    recorder->setDismissCallback([this]() { dismissAudioRecorder(); });
    recorder->setUseTakeCallback([this](const juce::File& take)
    {
        // Replacing the fullscreen widget destroys it, so finish the option
        // dispatch before loading the accepted take and changing layouts.
        juce::Component::SafePointer<UiHost> safeThis(this);
        juce::MessageManager::callAsync([safeThis, take]()
        {
            if (safeThis == nullptr || !take.existsAsFile())
                return;

            const int padIndex = juce::jmax(
                0, safeThis->audio.getSampler().getSelectedPad());
            auto& undo = safeThis->audio.getUndoManager();
            undo.beginNewTransaction("Load Sample");
            if (!undo.perform(new LoadSampleCommand(safeThis->audio, padIndex, take)))
            {
                safeThis->windowManager_.showToast(
                    DisplaySide::Right, ToastKind::Error,
                    "Could not load the recording");
                return;
            }

            safeThis->windowManager_.close("audio_recorder");
            safeThis->audioRecorderStashLeft_.reset();
            safeThis->audioRecorderStashRight_.reset();
            safeThis->openSamplingWorkflow();
        });
    });
    windowManager_.open(std::move(recorder), DisplaySide::Left);
    windowManager_.setFocus(DisplaySide::Left);
}

void UiHost::dismissAudioRecorder()
{
    windowManager_.close("audio_recorder");

    if (audioRecorderStashRight_)
        windowManager_.open(std::move(audioRecorderStashRight_), DisplaySide::Right);
    if (audioRecorderStashLeft_)
        windowManager_.open(std::move(audioRecorderStashLeft_), DisplaySide::Left);
}

void UiHost::openSlicedPadWorkflow(int sliceCount)
{
    // Leave the editor and enter the same pad-oriented layout as the hardware
    // Pad Mode button. Pattern remains useful as left-panel context, but open
    // Pad Overview last so it owns the physical pads and their LEDs.
    windowManager_.open(createPatternWidget(), DisplaySide::Left);
    auto padOverview = std::make_unique<PadOverviewWidget>();
    padOverview->setPadSelectedCallback([this](int /*padIndex*/)
    {
        windowManager_.open(createPadDetailsWidget(), DisplaySide::Left);
    });
    windowManager_.open(std::move(padOverview), DisplaySide::Right);
    windowManager_.setFocus(DisplaySide::Right);
    windowManager_.showToast(
        DisplaySide::Right, ToastKind::Success,
        juce::String(sliceCount) + " slices - PAD mode");
}

void UiHost::showSliceDetails(AudioEditorWidget& editor)
{
    if (auto* current = windowManager_.getWidget(DisplaySide::Right);
        current != nullptr && current->describe().id == "slice_details")
    {
        windowManager_.setFocus(DisplaySide::Left);
        return;
    }

    sliceDetailsStashRight_ = windowManager_.takeWidget(DisplaySide::Right);
    windowManager_.open(
        std::make_unique<SliceDetailsWidget>(editor),
        DisplaySide::Right);
    windowManager_.setFocus(DisplaySide::Left);
}

void UiHost::hideSliceDetails()
{
    auto* current = windowManager_.getWidget(DisplaySide::Right);
    if (current != nullptr && current->describe().id == "slice_details")
        windowManager_.close("slice_details");

    if (sliceDetailsStashRight_ != nullptr
        && windowManager_.getWidget(DisplaySide::Right) == nullptr)
        windowManager_.open(std::move(sliceDetailsStashRight_), DisplaySide::Right);

    if (auto* left = windowManager_.getWidget(DisplaySide::Left);
        left != nullptr && left->describe().id == "audio_editor")
        windowManager_.setFocus(DisplaySide::Left);
}

void UiHost::openBrowserForPad(int padId, bool addLayer)
{
    if (sampleBrowser_ != nullptr)
    {
        hideSampleBrowser();
        return;
    }

    showSampleBrowser();

    // Override the file-chosen callback to load onto the specific pad
    if (sampleBrowser_ != nullptr)
    {
        juce::Component::SafePointer<UiHost> safeThis(this);
        sampleBrowser_->setOnFileChosen([safeThis, padId, addLayer](const juce::File& file)
        {
            juce::MessageManager::callAsync([safeThis, padId, file, addLayer]() mutable
            {
                if (safeThis == nullptr)
                    return;
                safeThis->hideSampleBrowser();
                if (addLayer)
                {
                    const bool loaded = safeThis->audio.getSampler().addSampleLayer(
                        padId, file);
                    safeThis->windowManager_.showToast(
                        DisplaySide::Left,
                        loaded ? ToastKind::Success : ToastKind::Warning,
                        loaded ? ("Layer added: " + file.getFileNameWithoutExtension())
                               : juce::String("Could not add layer"));
                    return;
                }
                auto& undo = safeThis->audio.getUndoManager();
                undo.beginNewTransaction("Load Sample");
                undo.perform(new LoadSampleCommand(safeThis->audio, padId, file));
                safeThis->windowManager_.showToast(DisplaySide::Left, ToastKind::Success,
                    "Pad " + juce::String(padId + 1) + ": "
                        + file.getFileNameWithoutExtension());
            });
        });
    }
}

class UiHost::FileDialogListener : public juce::ValueTree::Listener
{
public:
    explicit FileDialogListener(UiHost& host) : host_(host) {}
    void valueTreePropertyChanged(juce::ValueTree& tree,
                                  const juce::Identifier& prop) override
    {
        if (prop != FileDialogStateKeys::kMode) return;
        const auto mode = tree.getProperty(prop).toString();
        if (mode != "browse") return;
        if (host_.windowManager_.getWidget(DisplaySide::Right) != nullptr) return;

        // Returning to Browse from Search/SaveAs — the T9 vacated Right.
        // Re-open the preview with the same rename/delete wiring the initial
        // Browse-mode open uses.
        host_.openFilePreviewOnRight();
    }
    UiHost& host_;
};

void UiHost::showFileDialog(FileBrowserWidget::Mode mode)
{
    // No-op if already open — protects the stash from being overwritten by
    // re-entry (e.g. shift+File pressed while the dialog is already up).
    if (auto* left = windowManager_.getWidget(DisplaySide::Left);
        left != nullptr && left->describe().id == "file_browser")
        return;

    // Stash whatever widgets are currently on the panels so we can restore
    // them on dismiss (instead of landing on wallpaper).
    stashedLeft_  = windowManager_.takeWidget(DisplaySide::Left);
    stashedRight_ = windowManager_.takeWidget(DisplaySide::Right);

    // Seed shared state
    auto settings = audio.getSettingsState();
    auto state = settings.getOrCreateChildWithName(FileDialogStateKeys::kRoot, nullptr);
    const auto modeName =
        mode == FileBrowserWidget::Mode::SaveAs ? juce::String("saveAs")
      : mode == FileBrowserWidget::Mode::Search ? juce::String("search")
      :                                           juce::String("browse");
    state.setProperty(FileDialogStateKeys::kMode, modeName, nullptr);

    // Attach ValueTree listener to re-open preview when mode returns to browse
    fileDialogState_ = state;
    fileDialogListener_ = std::make_unique<FileDialogListener>(*this);
    fileDialogState_.addListener(fileDialogListener_.get());

    // Open widgets
    auto browser = std::make_unique<FileBrowserWidget>();
    browser->setInitialMode(mode);

    // Raw pointer is valid because the unique_ptr stays alive inside the slot.
    auto* rawBrowser = browser.get();
    rawBrowser->setDismissRequested([this]() { dismissFileDialog(); });
    rawBrowser->setLoadRequested([this](const juce::File& file) {
        pushLoadConfirm(file);
    });

    windowManager_.open(std::move(browser), DisplaySide::Left);

    // In text-entry modes (SaveAs/Search) the browser's onActivated has
    // already put a T9 widget on the Right panel via TextInputComponent.
    // Opening the preview here would stomp it — defer. When the user
    // returns to Browse mode (commit or cancel) the FileDialogListener
    // notices Right is empty and opens the preview then.
    if (mode == FileBrowserWidget::Mode::Browse)
        openFilePreviewOnRight();

    // Opening the preview second leaves focus on the Right panel. Nav/knob
    // events fall back to the focused widget, so the browser's list would be
    // unreachable. Move focus back to the browser.
    windowManager_.setFocus(DisplaySide::Left);
}

void UiHost::openFilePreviewOnRight()
{
    auto* left = windowManager_.getWidget(DisplaySide::Left);
    auto* browser = dynamic_cast<FileBrowserWidget*>(left);
    if (browser == nullptr) return;

    auto preview = std::make_unique<FilePreviewWidget>();
    auto* rawPreview = preview.get();
    rawPreview->setOnRenameRequested([browser](const juce::File& file) {
        browser->beginRename(file);
    });
    rawPreview->setOnDeleteRequested([this, browser](const juce::File& file) {
        pushDeleteConfirm(file, browser);
    });
    windowManager_.open(std::move(preview), DisplaySide::Right);
}

void UiHost::pushDeleteConfirm(const juce::File& file, FileBrowserWidget* browser)
{
    showOptionDialog(
        "Delete Project",
        "Delete " + file.getFileNameWithoutExtension() + "?",
        {
            OptionDialogComponent::Button{
                .label = "Delete",
                .onClick = [this, file, browser]() {
                    hideOptionDialog();
                    if (file.existsAsFile()) file.deleteFile();
                    const auto companion = file.withFileExtension(".tracktionedit");
                    if (companion.existsAsFile()) companion.deleteFile();
                    if (browser) browser->refreshList();
                },
                .enabled = true
            },
            OptionDialogComponent::Button{
                .label = "Cancel",
                .onClick = [this]() { hideOptionDialog(); },
                .enabled = true
            }
        });
}

void UiHost::pushLoadConfirm(const juce::File& file)
{
    showOptionDialog(
        "Load Project",
        "Load " + file.getFileNameWithoutExtension() + "?",
        {
            OptionDialogComponent::Button{
                .label = "Load",
                .onClick = [this, file]() {
                    hideOptionDialog();

                    // Keep the previous panel widgets deactivated until the
                    // old Edit has been replaced. Restoring them first leaves
                    // mixer descriptors pointing into the Edit that loading
                    // is about to destroy.
                    auto restoreLeft = std::move(stashedLeft_);
                    auto restoreRight = std::move(stashedRight_);
                    dismissFileDialog();
                    audio.loadProjectFromFile(file);
                    if (restoreLeft)
                        windowManager_.open(std::move(restoreLeft), DisplaySide::Left);
                    if (restoreRight)
                        windowManager_.open(std::move(restoreRight), DisplaySide::Right);
                },
                .enabled = true
            },
            OptionDialogComponent::Button{
                .label = "Cancel",
                .onClick = [this]() { hideOptionDialog(); },
                .enabled = true
            }
        });
}

void UiHost::dismissFileDialog()
{
    // Tear down the host listener first, then deactivate both widgets while
    // the shared ValueTree is still alive. FileBrowserWidget::onDeactivated
    // removes its own listener from that same tree; clearing our final
    // reference before closeSlot ran left it dereferencing freed listener
    // state when File/Save was used to close the dialog.
    if (fileDialogListener_ && fileDialogState_.isValid())
        fileDialogState_.removeListener(fileDialogListener_.get());
    fileDialogListener_.reset();

    // Preview callbacks retain the browser pointer, so destroy preview first.
    windowManager_.close("file_preview");
    windowManager_.close("file_browser");

    auto settings = audio.getSettingsState();
    auto state = settings.getChildWithName(FileDialogStateKeys::kRoot);
    if (state.isValid())
        settings.removeChild(state, nullptr);
    fileDialogState_ = {};

    // Restore the widgets that were on the panels before the dialog opened.
    if (stashedLeft_)
        windowManager_.open(std::move(stashedLeft_), DisplaySide::Left);
    if (stashedRight_)
        windowManager_.open(std::move(stashedRight_), DisplaySide::Right);
}

// ── Keyboard mode ─────────────────────────────────────────────────────────

std::unique_ptr<PatternWidget> UiHost::createPatternWidget()
{
    auto widget = std::make_unique<PatternWidget>();
    widget->setOnPianoRollRequested(
        [this]() { openPianoRollForSelectedChannel(); });
    return widget;
}

void UiHost::openPianoRollForSelectedChannel()
{
    if (windowManager_.hasDialog())
        return;

    auto* pattern = dynamic_cast<PatternWidget*>(
        windowManager_.getWidget(DisplaySide::Left));
    if (pattern == nullptr)
        return;

    te::MidiClip* midiClip = nullptr;
    juce::String channelName;
    if (keyboardModeActive_)
    {
        auto& bank = audio.getKeyboardBank();
        if (bank.isOnEmptySlot())
            return;
        midiClip = bank.getMidiClip();
        channelName = bank.getActiveName();
    }
    else
    {
        const int padIndex = pattern->getSelectedChannelPadId();
        const auto* pad = audio.getSampler().getPad(padIndex);
        if (pad == nullptr || pad->patternClip == nullptr)
            return;
        midiClip = pad->patternClip.get();
        channelName = audio.getSampler().getPadName(padIndex);
        if (channelName.isEmpty())
            channelName = "Pad " + juce::String(padIndex + 1);
    }

    if (midiClip == nullptr)
        return;

    pianoRollStashLeft_ = windowManager_.takeWidget(DisplaySide::Left);
    auto widget = std::make_unique<PianoRollWidget>();
    widget->setMidiClip(midiClip, channelName);
    widget->setOnClose([this]() {
        // Defer close-and-restore so an option callback can finish before its
        // widget is moved back into the active panel.
        juce::MessageManager::callAsync([this]() {
            windowManager_.close("piano_roll");
            if (pianoRollStashLeft_)
                windowManager_.open(std::move(pianoRollStashLeft_),
                                    DisplaySide::Left);
        });
    });
    windowManager_.open(std::move(widget), DisplaySide::Left);
}

void UiHost::enterKeyboardMode()
{
    if (keyboardModeActive_) return;
    keyboardModeActive_ = true;
    updateKeyboardLed();

    // Tell PatternWidget (if currently on Left) to switch to the
    // Instruments channel filter so the sequencer rows reflect the
    // keyboard-bank world instead of the drum-kit world.
    if (auto* pw = dynamic_cast<PatternWidget*>(windowManager_.getWidget(DisplaySide::Left)))
        pw->setChannelMode(PatternWidget::ChannelMode::Instruments);

    keyboardStashRight_ = windowManager_.takeWidget(DisplaySide::Right);

    auto widget = std::make_unique<KeyboardWidget>();
    keyboardWidgetPtr_ = widget.get();

    auto& bank = audio.getKeyboardBank();

    keyboardWidgetPtr_->setOnPrevSlot([this, &bank]() {
        bank.prevSlot();
        updateKeyboardWidget();
    });
    keyboardWidgetPtr_->setOnNextSlot([this, &bank]() {
        bank.nextSlot();
        updateKeyboardWidget();
    });
    auto openInstrumentBrowser = [this, &bank]() {
        showPluginBrowser(PluginBrowserWidget::Filter::Instruments,
            [this, &bank](const juce::PluginDescription& desc) {
                const bool ok = bank.loadInstrument(desc);
                updateKeyboardWidget();
                windowManager_.showToast(DisplaySide::Right,
                    ok ? ToastKind::Success : ToastKind::Warning,
                    ok ? ("Loaded: " + desc.name)
                       : ("Failed to load: " + desc.name));
            });
    };

    keyboardWidgetPtr_->setOnLoadInstrument(openInstrumentBrowser);

    // Add (+) always appends: jump to the empty slot first so loadInstrument
    // push_backs instead of replacing the current one.
    keyboardWidgetPtr_->setOnAddInstrument([this, &bank, openInstrumentBrowser]() {
        bank.setActiveSlot(bank.getNumSlots());  // clamps to empty slot
        updateKeyboardWidget();
        openInstrumentBrowser();
    });
    keyboardWidgetPtr_->setOnDeleteSlot([this, &bank]() {
        if (bank.isOnEmptySlot())
            return;
        const auto name = bank.getActiveName();
        auto dialog = std::make_unique<ConfirmDialog>(
            juce::String("Delete Instrument"),
            juce::String("Delete ") + (name.isNotEmpty() ? name : juce::String("slot")) + "?",
            juce::String("Delete"),
            [this, &bank, name]() {
                bank.deleteActiveSlot();
                updateKeyboardWidget();
                windowManager_.showToast(DisplaySide::Right,
                    ToastKind::Info,
                    name.isNotEmpty() ? ("Deleted: " + name)
                                      : juce::String("Slot deleted"));
            });
        windowManager_.showDialog(std::move(dialog));
    });

    windowManager_.open(std::move(widget), DisplaySide::Right);
    updateKeyboardWidget();
}

void UiHost::exitKeyboardMode()
{
    if (!keyboardModeActive_) return;
    keyboardModeActive_ = false;
    sixteenVelocities_ = false;
    updateKeyboardLed();
    updateFixedVelocityLed();

    if (auto* pw = dynamic_cast<PatternWidget*>(windowManager_.getWidget(DisplaySide::Left)))
        pw->setChannelMode(PatternWidget::ChannelMode::Pads);

    // Cut any ringing voices on the keyboard track so notes don't sustain
    // after the mode flips off.
    auto& bank = audio.getKeyboardBank();
    audio.getKeyboardArp().allNotesOff();
    auto* track = bank.getTrack();
    for (size_t i = 0; i < keyboardHeldPitches_.size(); ++i)
    {
        auto& pitch = keyboardHeldPitches_[i];
        if (track != nullptr && pitch >= 0)
            track->injectLiveMidiMessage(
                juce::MidiMessage::noteOff(1, pitch), {});
        pitch = -1;
        keyboardPendingRec_[i].active = false;
    }

    windowManager_.close("keyboard");
    keyboardWidgetPtr_ = nullptr;
    if (keyboardStashRight_)
        windowManager_.open(std::move(keyboardStashRight_), DisplaySide::Right);
}

void UiHost::emergencyPanic()
{
    audio.panicAllNotes();

    for (size_t i = 0; i < keyboardHeldPitches_.size(); ++i)
    {
        keyboardHeldPitches_[i] = -1;
        keyboardPendingRec_[i].active = false;
        if (keyboardWidgetPtr_ != nullptr)
            keyboardWidgetPtr_->setPadFlashed(static_cast<int>(i), false);
    }

    control.setButtonBrightness("muteChoke", HardwareConstants::kLedBright);
    windowManager_.showToast(windowManager_.getFocus(), ToastKind::Warning,
                             "PANIC · All notes off");
    windowManager_.flushHardwareState();
}

void UiHost::updateKeyboardWidget()
{
    if (keyboardWidgetPtr_ == nullptr) return;
    const auto& bank = audio.getKeyboardBank();
    keyboardWidgetPtr_->setBasePitch(keyboardModeBasePitch_);
    keyboardWidgetPtr_->setVelocityMode(
        fixedVelocity_, sixteenVelocities_);
    keyboardWidgetPtr_->setSlotInfo(
        bank.getActiveSlot(), bank.getNumSlots(),
        bank.isOnEmptySlot(), bank.getActiveName());

    // Mirror the bank's active slot as PatternWidget's selected channel.
    // In Instruments mode the selected-channel-id is a bank slot index
    // (not a pad id), so this is a direct pass-through.
    if (auto* pw = dynamic_cast<PatternWidget*>(windowManager_.getWidget(DisplaySide::Left)))
    {
        if (! bank.isOnEmptySlot())
            pw->selectChannel(bank.getActiveSlot());
    }
}

void UiHost::updateModeLeds()
{
    const auto leftId = [this]()
    {
        if (auto* w = windowManager_.getWidget(DisplaySide::Left))
            return w->describe().id;
        return juce::String();
    }();

    const auto rightId = [this]()
    {
        if (auto* w = windowManager_.getWidget(DisplaySide::Right))
            return w->describe().id;
        return juce::String();
    }();

    auto* pattern = dynamic_cast<PatternWidget*>(
        windowManager_.getWidget(DisplaySide::Left));
    const bool stepModeActive = pattern != nullptr && pattern->isStepModeActive();
    const auto signature = leftId + "|" + rightId + "|"
                         + (stepModeActive ? "step" : "no-step");
    // Mode transitions write immediately. The one-second refresh retries a
    // failed transfer; successful values are suppressed by the output cache.
    if (signature == lastModeLedSignature_ && ++modeLedRefreshTicks_ < 60)
        return;
    lastModeLedSignature_ = signature;
    modeLedRefreshTicks_ = 0;
    control.beginLedBatch();

    const auto setLeftModeLed = [this, &leftId](const char* button,
                                                 const char* widgetId)
    {
        control.setButtonBrightness(button,
            leftId == widgetId ? HardwareConstants::kLedBright
                               : HardwareConstants::kLedDim);
    };

    setLeftModeLed("pattern", "pattern");
    setLeftModeLed("mixer", "mixer");
    control.setButtonBrightness("sampling",
        (leftId == "audio_editor" || leftId == "audio_recorder")
            ? HardwareConstants::kLedBright
            : HardwareConstants::kLedDim);
    setLeftModeLed("arranger", "arranger");

    control.setButtonBrightness("keyboard",
        rightId == "keyboard" ? HardwareConstants::kLedBright
                               : HardwareConstants::kLedDim);
    control.setButtonBrightness("padMode",
        (rightId == "pad_overview" || rightId == "pad_details")
            ? HardwareConstants::kLedBright
            : HardwareConstants::kLedDim);

    control.setButtonBrightness("step",
        stepModeActive
            ? HardwareConstants::kLedBright
            : HardwareConstants::kLedDim);
    updateFixedVelocityLed();
    control.endLedBatch();
}

void UiHost::updateKeyboardLed()
{
    updateModeLeds();
}

void UiHost::updateFixedVelocityLed()
{
    control.setButtonBrightness("fixedVel",
        (fixedVelocity_ || sixteenVelocities_)
            ? HardwareConstants::kLedBright
            : HardwareConstants::kLedDim);
}

juce::String UiHost::describeCurrentPadSource() const
{
    // Legacy helper — the widget now reads slot info directly from the bank
    // via updateKeyboardWidget. Kept for other call sites that may still
    // want a source label.
    return audio.getKeyboardBank().getActiveName();
}

void UiHost::toggleArp()
{
    auto& arp = audio.getKeyboardArp();
    auto cur = arp.getSettings();
    cur.mode = (cur.mode == Arpeggiator::Mode::Off)
        ? Arpeggiator::Mode::Up : Arpeggiator::Mode::Off;
    arp.setSettings(cur);
    windowManager_.showToast(DisplaySide::Right,
        ToastKind::Info,
        cur.mode == Arpeggiator::Mode::Off ? "Arp off" : "Arp on");
}

void UiHost::showArpDialog()
{
    if (windowManager_.hasDialog()) { windowManager_.dismissDialog(); return; }
    auto dialog = std::make_unique<ArpDialog>();
    dialog->setDismissCallback([this]() {
        windowManager_.dismissDialog();
        updateArpLed();
    });
    windowManager_.showDialog(std::move(dialog));
}

void UiHost::updateArpLed()
{
    const bool active = audio.getKeyboardArp().isActive();
    control.setButtonBrightness("noteRepeatArp",
        active ? HardwareConstants::kLedBright
               : HardwareConstants::kLedDim);
}

void UiHost::togglePluginEditor()
{
    if (pluginEditorOpen_)
    {
        windowManager_.close("plugin_editor");
        pluginEditorOpen_ = false;
        if (pluginEditorStashLeft_)
            windowManager_.open(std::move(pluginEditorStashLeft_), DisplaySide::Left);
        return;
    }

    auto& bank = audio.getKeyboardBank();
    if (bank.isOnEmptySlot())
    {
        windowManager_.showToast(DisplaySide::Right,
            ToastKind::Warning, "No plugin loaded");
        return;
    }
    auto* plugin = bank.getPlugin();
    if (plugin == nullptr)
    {
        windowManager_.showToast(DisplaySide::Right,
            ToastKind::Warning, "Plugin instance unavailable");
        return;
    }

    pluginEditorStashLeft_ = windowManager_.takeWidget(DisplaySide::Left);

    auto widget = std::make_unique<PluginEditorWidget>();
    widget->setPlugin(plugin);
    windowManager_.open(std::move(widget), DisplaySide::Left);
    pluginEditorOpen_ = true;
}

// Listener adapter kept alive for the duration of an open PluginBrowserWidget.
// Forwards the picked description into the caller-supplied callback and
// tears the browser down on both confirm and cancel paths. Defined in the
// .cpp to avoid leaking PluginBrowserWidget internals through UiHost.h.
class UiHost::PluginBrowserListener : public PluginBrowserWidget::Listener
{
public:
    PluginBrowserListener(UiHost& h,
                          std::function<void(const juce::PluginDescription&)> cb)
        : host_(h), cb_(std::move(cb)) {}

    void pluginSelected(const juce::PluginDescription& d) override
    {
        // Defer dismissal to avoid destroying the browser (and this listener)
        // while we're inside the browser's confirmSelection call stack.
        auto cb = cb_;
        auto& host = host_;
        juce::MessageManager::callAsync([cb, d, &host]() {
            if (cb) cb(d);
            host.dismissPluginBrowser();
        });
    }
    void browserCancelled() override
    {
        auto& host = host_;
        juce::MessageManager::callAsync([&host]() { host.dismissPluginBrowser(); });
    }

private:
    UiHost& host_;
    std::function<void(const juce::PluginDescription&)> cb_;
};

void UiHost::showPluginBrowser(PluginBrowserWidget::Filter filter,
                               std::function<void(const juce::PluginDescription&)> onPicked)
{
    // Re-entry guard — if the browser is already up, treat the second open as
    // a no-op so we don't clobber the stash.
    if (auto* left = windowManager_.getWidget(DisplaySide::Left);
        left != nullptr && left->describe().id == "plugin_browser")
        return;

    // Stash whatever widgets are currently on the panels so we can restore
    // them on dismiss (same pattern as the File dialog).
    pluginBrowserStashLeft_  = windowManager_.takeWidget(DisplaySide::Left);
    pluginBrowserStashRight_ = windowManager_.takeWidget(DisplaySide::Right);

    pluginBrowserListener_ = std::make_unique<PluginBrowserListener>(*this, std::move(onPicked));

    auto browser = std::make_unique<PluginBrowserWidget>(pluginCatalog, filter);
    browser->setListener(pluginBrowserListener_.get());

    windowManager_.open(std::move(browser), DisplaySide::Left);
    windowManager_.setFocus(DisplaySide::Left);
}

void UiHost::dismissPluginBrowser()
{
    windowManager_.close("plugin_browser");
    pluginBrowserListener_.reset();

    if (pluginBrowserStashLeft_)
        windowManager_.open(std::move(pluginBrowserStashLeft_), DisplaySide::Left);
    if (pluginBrowserStashRight_)
        windowManager_.open(std::move(pluginBrowserStashRight_), DisplaySide::Right);
}
