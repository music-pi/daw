#include <gtest/gtest.h>

#include "../src/control/ControllerHost.h"
#include "../src/ui/components/TextInputComponent.h"
#include "../src/ui/widget/T9Widget.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/ui/widget/Widget.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/T9Dictionary.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"

// A minimal concrete Widget subclass for hosting a TextInputComponent.
class HostWidget : public Widget
{
public:
    WidgetDescriptor describe() const override { return { "host", 1, false, DisplayConstraint::Any }; }
    void onActivated(int) override {}
    void onDeactivated() override {}
    std::vector<std::string> requiredResources(int) override { return {}; }
    std::vector<Option> getOptions(int) override { return {}; }
    std::vector<Knob> getKnobs(int) override { return {}; }
    void paintPage(juce::Graphics&, int, juce::Rectangle<int>) override {}
    TextInputComponent* textInput() { return &field_; }
    void activate() { addAndMakeVisible(field_); field_.setBounds(0, 0, 100, 40); }
private:
    TextInputComponent field_;
};

class TextInputComponentTests : public ::testing::Test
{
protected:
    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    WindowManager wm;
};

TEST_F(TextInputComponentTests, BeginEditOpensT9OnOppositePanel)
{
    wm.setAudioEngine(&harness.audio());

    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    hostRaw->textInput()->beginEdit();
    EXPECT_TRUE(hostRaw->textInput()->isEditing());
    auto* rightWidget = wm.getWidget(DisplaySide::Right);
    ASSERT_NE(rightWidget, nullptr);
    EXPECT_EQ(rightWidget->describe().id, "t9");
}

TEST_F(TextInputComponentTests, EndEditClosesT9AndFiresCommit)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    juce::String committed;
    hostRaw->textInput()->setOnCommit([&](juce::String s) { committed = s; });
    hostRaw->textInput()->setText("hello");
    hostRaw->textInput()->beginEdit();
    hostRaw->textInput()->endEdit(true);

    EXPECT_FALSE(hostRaw->textInput()->isEditing());
    EXPECT_EQ(committed, juce::String("hello"));
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), nullptr);
}

TEST_F(TextInputComponentTests, PadTapsProduceCharactersOnCommit)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    hostRaw->textInput()->beginEdit();
    auto* t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);

    // MK3 pad 13 is labeled "ABC" (phone 2). Pad 8 is "GHI" (phone 4).
    // pad 13 cycles a/b/c/2. pad 8 commits the pending letter then cycles g/h/i/4.
    ControllerHost::PadEvent e;
    e.pressed = true;
    e.pad = 13; t9->handlePad(e);    // pending 'a'
    e.pad = 13; t9->handlePad(e);    // pending 'b'
    e.pad =  8; t9->handlePad(e);    // commits 'b', pending 'g'
    // OK moved off pad 15 to an option button.
    for (auto& opt : t9->getOptions(0))
        if (opt.id == juce::String("t9.ok") && opt.onInvoke) { opt.onInvoke(); break; }

    EXPECT_EQ(hostRaw->textInput()->getText(), juce::String("bg"));
    EXPECT_FALSE(hostRaw->textInput()->isEditing());
}

TEST_F(TextInputComponentTests, DeleteRemovesLastCharacter)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    hostRaw->textInput()->setText("abc");
    hostRaw->textInput()->beginEdit();
    auto* t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);

    // MK3 pad 15 (top-right) is the del function key.
    ControllerHost::PadEvent e;
    e.pressed = true; e.pad = 15;
    t9->handlePad(e);

    EXPECT_EQ(hostRaw->textInput()->getText(), juce::String("ab"));
}

TEST_F(TextInputComponentTests, DictionaryCompletionsUpdateAfterCommit)
{
    wm.setAudioEngine(&harness.audio());
    harness.audio().getT9Dictionary().seed(
        "proj", juce::StringArray { "MyProject", "MySession" });

    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    TextInputComponent::Config cfg;
    cfg.dictionaryScope = "proj";
    hostRaw->textInput()->configure(cfg);
    hostRaw->textInput()->beginEdit();

    auto* t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);

    // MK3 pad 10 is labeled "MNO" (phone 6). Tapping it once gives 'm'; cursor-right commits.
    ControllerHost::PadEvent e; e.pressed = true;
    e.pad = 10; t9->handlePad(e);        // pending 'm'
    e.pad =  3; t9->handlePad(e);        // cursor-right commits 'm'

    EXPECT_EQ(hostRaw->textInput()->getText(), juce::String("m"));
    // Candidate list is populated from the dictionary
    EXPECT_FALSE(hostRaw->textInput()->getCandidates().empty());
}

TEST_F(TextInputComponentTests, KnobScrollsCandidatesWithWrap)
{
    wm.setAudioEngine(&harness.audio());
    harness.audio().getT9Dictionary().seed(
        "x", juce::StringArray { "alpha", "apple", "amber" });

    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    TextInputComponent::Config cfg; cfg.dictionaryScope = "x";
    hostRaw->textInput()->configure(cfg);
    hostRaw->textInput()->beginEdit();

    auto* t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);

    // MK3 pad 13 is labeled "ABC" (phone 2). First tap cycles to 'a'; cursor-right commits.
    ControllerHost::PadEvent e; e.pressed = true;
    e.pad = 13; t9->handlePad(e);
    e.pad =  3; t9->handlePad(e);        // commit 'a'
    ASSERT_EQ(hostRaw->textInput()->getCandidates().size(), 3u);

    // 3 ticks on a 3-entry list should wrap back to index 0
    t9->handleKnob(0, +1, 0, false);
    t9->handleKnob(0, +1, 0, false);
    t9->handleKnob(0, +1, 0, false);
    EXPECT_EQ(hostRaw->textInput()->getSelectedCandidateIndex(), 0);
}

TEST_F(TextInputComponentTests, CharFilterDropsDisallowedCharacters)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    TextInputComponent::Config cfg;
    cfg.charFilter = [](juce::juce_wchar c) { return c != ' '; };  // drop spaces
    hostRaw->textInput()->configure(cfg);
    hostRaw->textInput()->beginEdit();

    auto* t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);

    // MK3 pad 1 is space, pad 3 is cursor-right. Tap space then commit attempt.
    ControllerHost::PadEvent e; e.pressed = true;
    e.pad = 1; t9->handlePad(e);    // pending ' '
    e.pad = 3; t9->handlePad(e);    // cursor-right tries to commit ' '
    EXPECT_TRUE(hostRaw->textInput()->getText().isEmpty());
}

TEST_F(TextInputComponentTests, MaxLengthStopsAcceptingChars)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    TextInputComponent::Config cfg;
    cfg.maxLength = 3;
    hostRaw->textInput()->configure(cfg);
    hostRaw->textInput()->setText("abc");  // pre-fill to maxLength
    hostRaw->textInput()->beginEdit();

    auto* t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);

    // MK3 pad 8 is GHI; pad 3 is cursor-right.
    ControllerHost::PadEvent e; e.pressed = true;
    e.pad = 8; t9->handlePad(e);
    e.pad = 3; t9->handlePad(e);
    EXPECT_EQ(hostRaw->textInput()->getText().length(), 3);
}

TEST_F(TextInputComponentTests, OverlappingBeginEditIsRefused)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    hostRaw->textInput()->beginEdit();
    ASSERT_TRUE(hostRaw->textInput()->isEditing());
    // Second call while still editing should be a no-op.
    hostRaw->textInput()->beginEdit();
    EXPECT_TRUE(hostRaw->textInput()->isEditing());
    EXPECT_NE(wm.getWidget(DisplaySide::Right), nullptr);
}

TEST_F(TextInputComponentTests, ExternalCloseTreatedAsCancel)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    bool cancelled = false;
    hostRaw->textInput()->setOnCancel([&]() { cancelled = true; });
    hostRaw->textInput()->beginEdit();

    // WindowManager externally removes the T9 (e.g., a dialog was shown).
    wm.close("t9");

    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(hostRaw->textInput()->isEditing());
}

TEST_F(TextInputComponentTests, HappyPathTypeMyAndCommitRecordsInDictionary)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    TextInputComponent::Config cfg;
    cfg.dictionaryScope = "proj";
    hostRaw->textInput()->configure(cfg);

    juce::String committed;
    hostRaw->textInput()->setOnCommit([&](juce::String s) { committed = s; });

    hostRaw->textInput()->beginEdit();
    auto* t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);

    // Type "my":
    //   pad 10 = "MNO" (phone 6) → tap once → pending 'm'
    //   pad 3  = cursor-right → commit 'm'
    //   pad 6  = "WXYZ" (phone 9) → tap 3 times → cycles w → x → y
    //   d-option OK → commit 'y', endEdit (OK moved off pad 15 to an option button)
    ControllerHost::PadEvent e; e.pressed = true;
    e.pad = 10; t9->handlePad(e);       // pending 'm'
    e.pad =  3; t9->handlePad(e);       // commit 'm'
    e.pad =  6; t9->handlePad(e);       // pending 'w'
    e.pad =  6; t9->handlePad(e);       // pending 'x'
    e.pad =  6; t9->handlePad(e);       // pending 'y'
    for (auto& opt : t9->getOptions(0))
        if (opt.id == juce::String("t9.ok") && opt.onInvoke) { opt.onInvoke(); break; }

    EXPECT_EQ(committed, juce::String("my"));
    // Dictionary should now contain "my" under scope "proj"
    auto hits = harness.audio().getT9Dictionary().completionsFor("proj", "m", 5);
    ASSERT_GE(hits.size(), 1u);
    EXPECT_TRUE(hits[0].equalsIgnoreCase("my"));
}

TEST_F(TextInputComponentTests, ConfigureWhileEditingCancelsAndResetsState)
{
    wm.setAudioEngine(&harness.audio());
    auto host = std::make_unique<HostWidget>();
    auto* hostRaw = host.get();
    wm.open(std::move(host), DisplaySide::Left);
    hostRaw->activate();

    bool cancelled = false;
    hostRaw->textInput()->setOnCancel([&]() { cancelled = true; });

    hostRaw->textInput()->setText("old");
    hostRaw->textInput()->beginEdit();
    auto* t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);

    // Start a pending cycle so sm_ is mid-state. Pad 13 = phone-2 "ABC" → 'a'.
    ControllerHost::PadEvent e; e.pressed = true;
    e.pad = 13; t9->handlePad(e);        // pending 'a'

    // Re-configure mid-edit. The in-progress edit must cancel and sm_ reset.
    TextInputComponent::Config fresh;
    fresh.initialValue = "fresh";
    hostRaw->textInput()->configure(fresh);

    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(hostRaw->textInput()->isEditing());
    EXPECT_EQ(hostRaw->textInput()->getText(), juce::String("fresh"));
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), nullptr);

    // Subsequent begin/commit must work cleanly (sm_ not carrying the old pending cycle).
    hostRaw->textInput()->beginEdit();
    t9 = dynamic_cast<T9Widget*>(wm.getWidget(DisplaySide::Right));
    ASSERT_NE(t9, nullptr);
    e.pad = 13; t9->handlePad(e);        // pending 'a' — fresh start
    e.pad =  3; t9->handlePad(e);        // commit 'a'
    EXPECT_EQ(hostRaw->textInput()->getText(), juce::String("fresha"));
}

TEST_F(TextInputComponentTests, BeginEditRefusedWhenHostNotInSlot)
{
    wm.setAudioEngine(&harness.audio());

    // Host widget is constructed and has a child TextInputComponent, but it is
    // NOT opened in the WindowManager. beginEdit must refuse rather than
    // silently spawning T9 on the wrong panel via getSide's fallback.
    HostWidget orphan;
    orphan.activate();
    // Orphan has no parent, so findEnclosingWidget returns null → openWidget
    // asserts and no-ops. Wrap in a WindowManager-rooted but unopened parent to
    // simulate the "host exists in the tree but not in a slot" case.
    wm.addChildComponent(orphan);

    orphan.textInput()->beginEdit();
    EXPECT_FALSE(orphan.textInput()->isEditing());
    EXPECT_EQ(wm.getWidget(DisplaySide::Left), nullptr);
    EXPECT_EQ(wm.getWidget(DisplaySide::Right), nullptr);
}
