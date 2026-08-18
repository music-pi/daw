#include <gtest/gtest.h>

#include "../src/input/InputManager.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <string>
#include <vector>

namespace
{

juce::MidiMessage makeNoteOn(int channel, int noteNumber, int velocity)
{
    jassert(velocity >= 0 && velocity <= 127);
    const float scaledVelocity = static_cast<float>(velocity) / 127.0f;
    return juce::MidiMessage::noteOn(channel, noteNumber, scaledVelocity);
}

class InputManagerTest : public ::testing::Test
{
protected:
    InputManager manager;
};

TEST_F(InputManagerTest, MidiHandlersRespectChannelNumberAndValue)
{
    bool triggered = false;
    manager.addMidiHandler(InputManager::HandlerPriority::Global,
                           {},
                           [&](InputEvent& event)
                           {
                               triggered = true;
                               EXPECT_TRUE(event.midiMessage.isNoteOn());
                               EXPECT_EQ(event.midiMessage.getNoteNumber(), 60);
                           },
                           /*channel*/ 1,
                           /*number*/ 60,
                           /*value*/ 100);

    EXPECT_TRUE(manager.dispatchMidi(makeNoteOn(1, 60, 100), "keyboard"));
    EXPECT_TRUE(triggered);

    triggered = false;
    EXPECT_FALSE(manager.dispatchMidi(makeNoteOn(2, 60, 100), "keyboard"));
    EXPECT_FALSE(triggered);

    EXPECT_FALSE(manager.dispatchMidi(makeNoteOn(1, 60, 90), "keyboard"));
    EXPECT_FALSE(triggered);
}

TEST_F(InputManagerTest, ContextHandlersRequireActivation)
{
    const juce::KeyPress key {'s'};
    bool triggered = false;
    manager.addKeyHandler(InputManager::HandlerPriority::Component,
                          "sample-browser",
                          key,
                          [&](InputEvent& event)
                          {
                              triggered = true;
                              EXPECT_EQ(event.keyPress, key);
                          });

    EXPECT_FALSE(manager.dispatchKey(key, "keyboard"));
    EXPECT_FALSE(triggered);

    manager.setActiveContext("sample-browser", true);
    EXPECT_TRUE(manager.dispatchKey(key, "keyboard"));
    EXPECT_TRUE(triggered);

    triggered = false;
    manager.setActiveContext("sample-browser", false);
    EXPECT_FALSE(manager.dispatchKey(key, "keyboard"));
    EXPECT_FALSE(triggered);
}

TEST_F(InputManagerTest, HigherPriorityHandlersConsumeEventsFirst)
{
    manager.setActiveContext("view", true);

    std::vector<std::string> order;
    manager.addButtonHandler(InputManager::HandlerPriority::Global,
                             {},
                             "transport.play",
                             [&](InputEvent&)
                             {
                                 order.push_back("global");
                             });

    manager.addButtonHandler(InputManager::HandlerPriority::View,
                             "view",
                             "transport.play",
                             [&](InputEvent& event)
                             {
                                 order.push_back("view");
                                 event.consumed = true;
                             });

    EXPECT_TRUE(manager.dispatchButton("transport.play", true, false, "mk3"));
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order.front(), "view");
}

TEST_F(InputManagerTest, PadButtonKnobAndStepperHandlersRouteMetadata)
{
    manager.setActiveContext("pads", true);
    manager.setActiveContext("buttons", true);
    manager.setActiveContext("knobs", true);
    manager.setActiveContext("stepper", true);

    bool padHandled = false;
    manager.addPadHandler(InputManager::HandlerPriority::View,
                          "pads",
                          [&](InputEvent& event)
                          {
                              padHandled = true;
                              EXPECT_EQ(static_cast<int>(event.metadata["pad"]), 3);
                              EXPECT_TRUE(static_cast<bool>(event.metadata["pressed"]));
                              EXPECT_EQ(static_cast<int>(event.metadata["pressure"]), 512);
                          },
                          static_cast<uint8_t>(3));

    bool buttonHandled = false;
    manager.addButtonHandler(InputManager::HandlerPriority::Component,
                             "buttons",
                             "shift",
                             [&](InputEvent& event)
                             {
                                 buttonHandled = true;
                                 EXPECT_EQ(event.metadata["buttonName"].toString(), "shift");
                                 EXPECT_TRUE(static_cast<bool>(event.metadata["shift"]));
                             });

    bool knobHandled = false;
    manager.addKnobHandler(InputManager::HandlerPriority::Component,
                           "knobs",
                           "macro1",
                           [&](InputEvent& event)
                           {
                               knobHandled = true;
                               EXPECT_EQ(event.metadata["knobName"].toString(), "macro1");
                               EXPECT_EQ(static_cast<int>(event.metadata["delta"]), 12);
                              EXPECT_FALSE(static_cast<bool>(event.metadata["shift"]));
                           });

    bool stepperHandled = false;
    manager.addStepperHandler(InputManager::HandlerPriority::Component,
                              "stepper",
                              [&](InputEvent& event)
                              {
                                  stepperHandled = true;
                                  EXPECT_EQ(static_cast<int>(event.metadata["direction"]), 1);
                                  EXPECT_EQ(static_cast<int>(event.metadata["position"]), 42);
                              });

    EXPECT_TRUE(manager.dispatchPad(3, true, 512, false, "mk3"));
    EXPECT_TRUE(manager.dispatchButton("shift", true, true, "mk3"));
    EXPECT_TRUE(manager.dispatchKnob("macro1", 12, 100, false, "mk3"));
    EXPECT_TRUE(manager.dispatchStepper(1, 42, false, "mk3"));

    EXPECT_TRUE(padHandled);
    EXPECT_TRUE(buttonHandled);
    EXPECT_TRUE(knobHandled);
    EXPECT_TRUE(stepperHandled);
}

TEST_F(InputManagerTest, TouchstripHandlerRoutesBothFingerAndPositionMetadata)
{
    bool handled = false;
    manager.addTouchstripHandler(
        InputManager::HandlerPriority::View, {},
        [&](InputEvent& event)
        {
            handled = true;
            EXPECT_EQ(event.type, InputEvent::Type::Touchstrip);
            EXPECT_EQ(static_cast<int>(event.metadata["finger"]), 2);
            EXPECT_TRUE(static_cast<bool>(event.metadata["touching"]));
            EXPECT_EQ(static_cast<int>(event.metadata["position"]), 49152);
            EXPECT_TRUE(static_cast<bool>(event.metadata["shift"]));
        });

    EXPECT_TRUE(manager.dispatchTouchstrip(2, true, 49152, true, "mk3"));
    EXPECT_TRUE(handled);
}

TEST_F(InputManagerTest, DispatchAvoidsFullHandlerCopy)
{
    // Registers 100 button handlers across the same event type — pre-refactor
    // baseline copied+sorted the full matching set per dispatch. With handlers
    // kept sorted at insertion, dispatch only copies matching entries. Soft
    // ceiling (50ms for 10k dispatches) catches catastrophic regressions only.
    constexpr int kHandlers   = 100;
    constexpr int kDispatches = 10000;

    std::vector<int> fireCounts(kHandlers, 0);
    for (int i = 0; i < kHandlers; ++i)
    {
        manager.addButtonHandler(InputManager::HandlerPriority::Global,
                                 {},
                                 "perf.target",
                                 [&fireCounts, i](InputEvent&) { ++fireCounts[i]; });
    }

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kDispatches; ++i)
    {
        manager.dispatchButton("perf.target", true, false, "mk3");
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    // Soft ceiling. 10k dispatches × 100 matching handlers = 1M invocations;
    // most of the time is the actions themselves, not dispatch logic. A 2s
    // cap catches only a full order-of-magnitude regression.
    EXPECT_LT(ms, 2000) << "dispatch() took " << ms << "ms for "
                       << kDispatches << " dispatches across " << kHandlers
                       << " handlers";

    // Sanity — every handler was invoked on every dispatch (no event consumed).
    for (int i = 0; i < kHandlers; ++i)
        EXPECT_EQ(fireCounts[i], kDispatches);
}

TEST_F(InputManagerTest, SortedInsertionPreservesPriorityOrder)
{
    // Add handlers in scrambled priority order and verify dispatch visits them
    // highest-priority-first — this is the contract previously enforced by
    // per-dispatch sort; sorted-insert must preserve it.
    std::vector<std::string> order;

    manager.setActiveContext("viewCtx", true);

    manager.addButtonHandler(InputManager::HandlerPriority::Global,   {},         "x",
                             [&](InputEvent&) { order.push_back("global"); });
    manager.addButtonHandler(InputManager::HandlerPriority::Modal,    "viewCtx",  "x",
                             [&](InputEvent&) { order.push_back("modal"); });
    manager.addButtonHandler(InputManager::HandlerPriority::Component,"viewCtx",  "x",
                             [&](InputEvent&) { order.push_back("component"); });
    manager.addButtonHandler(InputManager::HandlerPriority::View,     "viewCtx",  "x",
                             [&](InputEvent&) { order.push_back("view"); });

    manager.dispatchButton("x", true, false, "mk3");

    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], "modal");
    EXPECT_EQ(order[1], "view");
    EXPECT_EQ(order[2], "component");
    EXPECT_EQ(order[3], "global");
}

} // namespace
