#include <gtest/gtest.h>
#include "../src/engine/MidiStepView.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace {
std::unique_ptr<te::MidiList> makeEmptyList()
{
    auto list = std::make_unique<te::MidiList>();
    list->setMidiChannel (te::MidiChannel (1));
    return list;
}
}

TEST(MidiStepViewTests, EmptyListHasNoStepsOn)
{
    auto list = makeEmptyList();
    MidiStepView view { *list, /*rootPitch*/ 36, /*stepsPerBar*/ 16, /*bars*/ 1 };
    for (int i = 0; i < 16; ++i)
        EXPECT_FALSE(view.isStepOn(i));
}

TEST(MidiStepViewTests, SetStepOnAddsANoteAtRootPitch)
{
    auto list = makeEmptyList();
    MidiStepView view { *list, 36, 16, 1 };

    view.setStep(4, true, /*velocity*/ 100);

    EXPECT_TRUE(view.isStepOn(4));
    EXPECT_EQ(view.getStepVelocity(4), 100);
    ASSERT_EQ(list->getNumNotes(), 1);
    EXPECT_EQ(list->getNotes()[0]->getNoteNumber(), 36);
}

TEST(MidiStepViewTests, SetStepOffRemovesTheRootPitchNoteAtThatStep)
{
    auto list = makeEmptyList();
    MidiStepView view { *list, 36, 16, 1 };
    view.setStep(2, true, 80);
    view.setStep(2, false, 0);
    EXPECT_FALSE(view.isStepOn(2));
    EXPECT_EQ(list->getNumNotes(), 0);
}

TEST(MidiStepViewTests, NonRootNotesAreInvisibleToStepView)
{
    auto list = makeEmptyList();
    const double stepBeats = 0.25;  // 16 steps per bar
    list->addNote (48, tracktion::BeatPosition::fromBeats (4 * stepBeats),
                   tracktion::BeatDuration::fromBeats (stepBeats * 0.5),
                   100, 0, nullptr);

    MidiStepView view { *list, 36, 16, 1 };
    for (int i = 0; i < 16; ++i)
        EXPECT_FALSE(view.isStepOn(i)) << "step " << i << " should be off";
}

TEST(MidiStepViewTests, VelocityIsPreservedOnReadBack)
{
    auto list = makeEmptyList();
    MidiStepView view { *list, 36, 16, 1 };
    view.setStep(7, true, 42);
    EXPECT_EQ(view.getStepVelocity(7), 42);
}
