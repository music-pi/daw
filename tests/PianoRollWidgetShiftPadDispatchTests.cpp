// End-to-end: shift+pad events reach PianoRollWidget's Modal-priority
// handler and plain pad events fall through to Global handlers. Guards
// against regressions in the InputManager priority dispatch + WindowManager
// wiring when the piano roll is active.

#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"
#include "../src/engine/SamplerInstrument.h"
#include "../src/input/InputManager.h"
#include "../src/ui/widget/PianoRollWidget.h"
#include "../src/ui/widget/WindowManager.h"

#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

namespace te = tracktion::engine;

namespace {
te::MidiClip* firstClipOfPad(testharness::EngineHarness& h, int pad)
{
    auto* track = h.pads().getTrack(pad);
    for (auto* c : track->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(c)) return m;
    return nullptr;
}
}

TEST(PianoRollWidgetShiftPadDispatch, ShiftPlusPadReachesPianoRollHandler)
{
    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    InputManager inputManager;
    testharness::MockControllerHost mockController;
    mockController.setInputManager(&inputManager);

    WindowManager wm;
    wm.setAudioEngine(&harness.audio());
    wm.setControllerHost(&mockController);

    auto* mc = firstClipOfPad(harness, 0);
    ASSERT_NE(mc, nullptr);
    mc->getSequence().addNote(
        60, tracktion::BeatPosition::fromBeats(0.0),
        tracktion::BeatDuration::fromBeats(0.25), 100, 0, nullptr);

    auto widget = std::make_unique<PianoRollWidget>();
    widget->setMidiClip(mc, "test");
    wm.open(std::move(widget), DisplaySide::Left);

    // Register sentinels at both Modal (should fire) and Global (should NOT
    // fire — the widget consumes on any shift event).
    int modalSawShiftPad = 0;
    int globalSawShiftPad = 0;
    inputManager.addPadHandler(
        InputManager::HandlerPriority::Modal, "",
        [&](InputEvent& e) {
            if (e.isShift() && e.isPressed() && e.padIndex() == 6)
                ++modalSawShiftPad;
        });
    inputManager.addPadHandler(
        InputManager::HandlerPriority::Global, "",
        [&](InputEvent& e) {
            if (e.isShift() && e.isPressed() && e.padIndex() == 6)
                ++globalSawShiftPad;
        });

    ControllerHost::PadEvent padEvent;
    padEvent.pad = 6;
    padEvent.pressed = true;
    padEvent.pressure = 1000;
    padEvent.shift = true;
    padEvent.macro = false;

    mockController.handlePadEvent(padEvent);

    EXPECT_EQ(modalSawShiftPad, 1) << "Modal-priority handler did not fire for shift+pad 6";
    EXPECT_EQ(globalSawShiftPad, 0) << "event should have been consumed before reaching Global";
}

TEST(PianoRollWidgetShiftPadDispatch, PlainPadFallsThroughToGlobal)
{
    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    InputManager inputManager;
    testharness::MockControllerHost mockController;
    mockController.setInputManager(&inputManager);

    WindowManager wm;
    wm.setAudioEngine(&harness.audio());
    wm.setControllerHost(&mockController);

    auto* mc = firstClipOfPad(harness, 0);
    ASSERT_NE(mc, nullptr);

    auto widget = std::make_unique<PianoRollWidget>();
    widget->setMidiClip(mc, "test");
    wm.open(std::move(widget), DisplaySide::Left);

    int globalSaw = 0;
    inputManager.addPadHandler(
        InputManager::HandlerPriority::Global, "",
        [&](InputEvent& e) { if (! e.isShift()) ++globalSaw; });

    ControllerHost::PadEvent padEvent{};
    padEvent.pad = 3;
    padEvent.pressed = true;
    padEvent.pressure = 1000;
    padEvent.shift = false;
    mockController.handlePadEvent(padEvent);

    EXPECT_EQ(globalSaw, 1) << "non-shift pad should pass through the Modal handler";
}

TEST(PianoRollWidgetControls, ExposesAdjustableQuantiseAndFineNudge)
{
    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    harness.createEmptyEdit();

    WindowManager manager;
    manager.setAudioEngine(&harness.audio());
    auto* clip = firstClipOfPad(harness, 0);
    ASSERT_NE(clip, nullptr);
    clip->getSequence().addNote(
        60, tracktion::BeatPosition::fromBeats(0.30),
        tracktion::BeatDuration::fromBeats(0.25), 100, 0, nullptr);

    auto widget = std::make_unique<PianoRollWidget>();
    auto* raw = widget.get();
    widget->setMidiClip(clip, "test");
    manager.open(std::move(widget), DisplaySide::Left);

    auto options = raw->getOptions(0);
    ASSERT_EQ(options.size(), 4u);
    ASSERT_TRUE(options[1].onInvoke);
    options[1].onInvoke(); // Select All

    raw->handleKnob(0, -3, 0, false); // Quantise strength 100 -> 95.
    options = raw->getOptions(0);
    EXPECT_TRUE(options[2].label.contains("95%"));

    raw->handleKnob(1, 3, 0, true); // Shift fine nudge = +1/256 beat.
    auto notes = clip->getSequence().getNotes();
    ASSERT_EQ(notes.size(), 1);
    EXPECT_NEAR(notes[0]->getStartBeat().inBeats(),
                0.30 + 1.0 / 256.0, 1e-9);

    const auto knobs = raw->getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[0].label, "Quantize");
    EXPECT_EQ(knobs[1].label, "Nudge");
    EXPECT_EQ(knobs[2].label, "Grid");
}
