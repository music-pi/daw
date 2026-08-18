// tests/T9StateMachineTests.cpp
#include <gtest/gtest.h>
#include "../src/ui/components/T9StateMachine.h"

TEST(T9StateMachineTests, IdlePadTapEntersCyclingAtIndexZero)
{
    T9StateMachine sm;
    auto out = sm.onPadTap(2);  // pad 2 = "ABC2" in letters mode
    EXPECT_FALSE(out.emitCommit);
    EXPECT_TRUE(out.hasPendingCycle);
    EXPECT_EQ(out.pendingPadIndex, 2);
    EXPECT_EQ(out.pendingPreviewChar, 'a');
}

TEST(T9StateMachineTests, SamePadTapAdvancesCycle)
{
    T9StateMachine sm;
    sm.onPadTap(2);                 // 'a'
    auto out = sm.onPadTap(2);      // 'b'
    EXPECT_FALSE(out.emitCommit);
    EXPECT_EQ(out.pendingPreviewChar, 'b');
    out = sm.onPadTap(2);           // 'c'
    EXPECT_EQ(out.pendingPreviewChar, 'c');
}

TEST(T9StateMachineTests, SamePadWrapsAroundAfterFullCycle)
{
    T9StateMachine sm;
    sm.onPadTap(2);  // 'a'
    sm.onPadTap(2);  // 'b'
    sm.onPadTap(2);  // 'c'
    sm.onPadTap(2);  // '2'
    auto out = sm.onPadTap(2); // back to 'a'
    EXPECT_EQ(out.pendingPreviewChar, 'a');
    EXPECT_FALSE(out.emitCommit);   // wrap-around does not commit
}

TEST(T9StateMachineTests, DifferentPadCommitsPreviousAndStartsNewCycle)
{
    T9StateMachine sm;
    sm.onPadTap(2);                        // pending = 'a'
    sm.onPadTap(2);                        // pending = 'b'
    auto out = sm.onPadTap(4);             // pad 4 = GHI4
    EXPECT_TRUE(out.emitCommit);
    EXPECT_EQ(out.committedChar, 'b');
    EXPECT_TRUE(out.hasPendingCycle);
    EXPECT_EQ(out.pendingPadIndex, 4);
    EXPECT_EQ(out.pendingPreviewChar, 'g');
}

TEST(T9StateMachineTests, TimeoutCommitsPendingAndReturnsToIdle)
{
    T9StateMachine sm;
    sm.onPadTap(2); sm.onPadTap(2);        // pending = 'b'
    auto out = sm.onTimeout();
    EXPECT_TRUE(out.emitCommit);
    EXPECT_EQ(out.committedChar, 'b');
    EXPECT_FALSE(out.hasPendingCycle);

    // After timeout, another tap starts fresh at index 0
    auto next = sm.onPadTap(2);
    EXPECT_FALSE(next.emitCommit);
    EXPECT_EQ(next.pendingPreviewChar, 'a');
}

TEST(T9StateMachineTests, TimeoutWithNoPendingIsNoop)
{
    T9StateMachine sm;
    auto out = sm.onTimeout();
    EXPECT_FALSE(out.emitCommit);
    EXPECT_FALSE(out.hasPendingCycle);
}

TEST(T9StateMachineTests, CursorRightCommitsPendingAndReturnsToIdle)
{
    T9StateMachine sm;
    sm.onPadTap(2); sm.onPadTap(2);   // 'b' pending
    auto out = sm.onCursorRight();
    EXPECT_TRUE(out.emitCommit);
    EXPECT_EQ(out.committedChar, 'b');
    EXPECT_EQ(out.cursorDelta, 1);
    EXPECT_FALSE(out.hasPendingCycle);
}

TEST(T9StateMachineTests, CursorRightIdleJustMovesCursor)
{
    T9StateMachine sm;
    auto out = sm.onCursorRight();
    EXPECT_FALSE(out.emitCommit);
    EXPECT_EQ(out.cursorDelta, 1);
}

TEST(T9StateMachineTests, CursorLeftAlwaysMovesCursorNoCommit)
{
    T9StateMachine sm;
    sm.onPadTap(2); sm.onPadTap(2);   // 'b' pending
    auto out = sm.onCursorLeft();
    // Design: cursor-left does not commit; discards cycle like delete.
    EXPECT_FALSE(out.emitCommit);
    EXPECT_EQ(out.cursorDelta, -1);
    EXPECT_FALSE(out.hasPendingCycle);
}

TEST(T9StateMachineTests, DeleteMidCycleDiscardsNoBufferChange)
{
    T9StateMachine sm;
    sm.onPadTap(2); sm.onPadTap(2);   // 'b' pending
    auto out = sm.onDelete();
    EXPECT_FALSE(out.emitBackspace);
    EXPECT_FALSE(out.hasPendingCycle);
}

TEST(T9StateMachineTests, DeleteIdleEmitsBackspace)
{
    T9StateMachine sm;
    auto out = sm.onDelete();
    EXPECT_TRUE(out.emitBackspace);
}

TEST(T9StateMachineTests, OkCommitsPendingAndSignalsEndEditTrue)
{
    T9StateMachine sm;
    sm.onPadTap(2);                    // 'a' pending
    auto out = sm.onOk();
    EXPECT_TRUE(out.emitCommit);
    EXPECT_EQ(out.committedChar, 'a');
    EXPECT_TRUE(out.endEdit);
    EXPECT_TRUE(out.endEditCommitValue);
    EXPECT_FALSE(out.hasPendingCycle);
}

TEST(T9StateMachineTests, OkIdleSignalsEndEditWithoutCommit)
{
    T9StateMachine sm;
    auto out = sm.onOk();
    EXPECT_FALSE(out.emitCommit);
    EXPECT_TRUE(out.endEdit);
    EXPECT_TRUE(out.endEditCommitValue);
}

TEST(T9StateMachineTests, CapsCyclesLowerTitleUpperLower)
{
    T9StateMachine sm;
    EXPECT_EQ(sm.getCaps(), T9StateMachine::Caps::Lower);

    auto o = sm.onCapsTap();
    EXPECT_TRUE(o.capsCycled);
    EXPECT_EQ(sm.getCaps(), T9StateMachine::Caps::Title);

    sm.onCapsTap();
    EXPECT_EQ(sm.getCaps(), T9StateMachine::Caps::Upper);

    sm.onCapsTap();
    EXPECT_EQ(sm.getCaps(), T9StateMachine::Caps::Lower);
}

TEST(T9StateMachineTests, TitleApplySingleCharThenReverts)
{
    T9StateMachine sm;
    sm.onCapsTap();                      // Title
    EXPECT_EQ(sm.getCaps(), T9StateMachine::Caps::Title);

    sm.onPadTap(2);                      // pending = 'A' (Title applied)
    auto commit = sm.onCursorRight();    // commit 'A'
    EXPECT_EQ(commit.committedChar, 'A');

    // After first commit, caps reverts to Lower
    EXPECT_EQ(sm.getCaps(), T9StateMachine::Caps::Lower);

    sm.onPadTap(2);
    auto next = sm.onCursorRight();
    EXPECT_EQ(next.committedChar, 'a');  // back to lower
}

TEST(T9StateMachineTests, UpperStaysStickyAcrossCommits)
{
    T9StateMachine sm;
    sm.onCapsTap(); sm.onCapsTap();      // Upper

    sm.onPadTap(2);
    auto c1 = sm.onCursorRight();        // 'A'
    EXPECT_EQ(c1.committedChar, 'A');

    sm.onPadTap(2);
    auto c2 = sm.onCursorRight();        // still 'A' — upper sticky
    EXPECT_EQ(c2.committedChar, 'A');
    EXPECT_EQ(sm.getCaps(), T9StateMachine::Caps::Upper);
}

TEST(T9StateMachineTests, SymTogglesMode)
{
    T9StateMachine sm;
    EXPECT_EQ(sm.getMode(), T9StateMachine::Mode::Letters);

    auto o = sm.onSymTap();
    EXPECT_TRUE(o.symToggle);
    EXPECT_EQ(sm.getMode(), T9StateMachine::Mode::Symbols);

    sm.onSymTap();
    EXPECT_EQ(sm.getMode(), T9StateMachine::Mode::Letters);
}

TEST(T9StateMachineTests, SymbolModePad2ProducesHashThenAt)
{
    T9StateMachine sm;
    sm.onSymTap();                       // symbols
    sm.onPadTap(2);
    auto out = sm.onCursorRight();       // commit
    EXPECT_EQ(out.committedChar, '#');

    sm.onPadTap(2); sm.onPadTap(2);      // cycle to '@'
    auto out2 = sm.onCursorRight();
    EXPECT_EQ(out2.committedChar, '@');
}

TEST(T9StateMachineTests, SymTapDiscardsPendingCycle)
{
    T9StateMachine sm;
    sm.onPadTap(2);                      // 'a' pending
    auto sym = sm.onSymTap();
    EXPECT_TRUE(sym.symToggle);
    // Pending cycle in letters mode should not commit into buffer on mode swap
    EXPECT_FALSE(sym.emitCommit);

    // Next pad tap is in symbol mode starting at index 0
    auto out = sm.onPadTap(2);
    EXPECT_EQ(out.pendingPreviewChar, '#');
}
