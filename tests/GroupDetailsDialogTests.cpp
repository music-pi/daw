#include <gtest/gtest.h>

#include "../src/ui/widget/GroupDetailsDialog.h"
#include "../src/ui/widget/WindowManager.h"
#include "../src/engine/AudioEngine.h"
#include "../src/engine/GroupManager.h"
#include "../src/engine/SamplerInstrument.h"
#include "harness/EngineHarness.h"
#include "harness/JuceHarness.h"
#include "harness/MockControllerHost.h"

namespace
{

class GroupDetailsDialogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        harness.createEmptyEdit();
        wm.setAudioEngine(&harness.audio());
    }

    testharness::JuceFrameworkContext juceContext;
    testharness::EngineHarness harness;
    testharness::MockControllerHost controller;
    WindowManager wm;
};

// 1. Descriptor
TEST_F(GroupDetailsDialogTest, DescriptorIsCorrect)
{
    GroupDetailsDialog dialog;
    auto desc = dialog.describe();

    EXPECT_EQ(desc.id, "group_details");
    EXPECT_EQ(desc.pageCount, 1);
    EXPECT_TRUE(desc.forceOnTop);
    EXPECT_EQ(desc.display, DisplayConstraint::Any);
}

// 2. Resources -- returns d1-d4 and k1-k4
TEST_F(GroupDetailsDialogTest, ResourcesReturnOptionsAndKnobs)
{
    GroupDetailsDialog dialog;
    auto resources = dialog.requiredResources(0);

    EXPECT_EQ(resources.size(), 8u);

    for (int i = 1; i <= 4; ++i)
    {
        EXPECT_NE(std::find(resources.begin(), resources.end(), "d" + std::to_string(i)), resources.end())
            << "Missing d" << i;
        EXPECT_NE(std::find(resources.begin(), resources.end(), "k" + std::to_string(i)), resources.end())
            << "Missing k" << i;
    }

    // Should NOT have pad LEDs
    for (int i = 1; i <= 16; ++i)
        EXPECT_EQ(std::find(resources.begin(), resources.end(), "p" + std::to_string(i)), resources.end())
            << "Should not have p" << i;
}

// 3. Options for empty group shows Close and Create
TEST_F(GroupDetailsDialogTest, OptionsForEmptyGroupShowCloseAndCreate)
{
    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(3);  // Group 4, should be empty

    wm.showDialog(std::move(dialog));

    auto* raw = static_cast<GroupDetailsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto opts = raw->getOptions(0);
    ASSERT_GE(opts.size(), 2u);
    EXPECT_EQ(opts[0].id, "close");
    EXPECT_EQ(opts[1].id, "create");

    wm.dismissDialog();
}

// 4. Options for existing group shows Close and Delete
TEST_F(GroupDetailsDialogTest, OptionsForExistingGroupShowCloseAndDelete)
{
    auto& gm = harness.audio().getGroupManager();
    gm.createGroup(2);
    gm.saveCurrentState(2);

    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(2);

    wm.showDialog(std::move(dialog));

    auto* raw = static_cast<GroupDetailsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto opts = raw->getOptions(0);
    ASSERT_GE(opts.size(), 2u);
    EXPECT_EQ(opts[0].id, "close");
    EXPECT_EQ(opts[1].id, "delete");

    wm.dismissDialog();
}

// 5. Delete confirmation flow
TEST_F(GroupDetailsDialogTest, DeleteConfirmationFlow)
{
    auto& gm = harness.audio().getGroupManager();
    gm.createGroup(1);
    gm.saveCurrentState(1);

    EXPECT_TRUE(gm.hasGroupData(1));

    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(1);

    wm.showDialog(std::move(dialog));

    auto* raw = static_cast<GroupDetailsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    // Invoke delete option (index 1)
    raw->handleOption(1);

    // Should now be in confirmation mode
    EXPECT_TRUE(raw->isConfirmingDelete());

    auto confirmOpts = raw->getOptions(0);
    ASSERT_EQ(confirmOpts.size(), 4u);
    EXPECT_EQ(confirmOpts[0].id, "cancel");
    EXPECT_EQ(confirmOpts[3].id, "confirm");

    // Cancel should exit confirmation mode
    raw->handleOption(0);
    EXPECT_FALSE(raw->isConfirmingDelete());

    wm.dismissDialog();
}

// 6. Delete confirmation performs delete
TEST_F(GroupDetailsDialogTest, ConfirmDeleteClearsGroup)
{
    auto& gm = harness.audio().getGroupManager();
    gm.createGroup(1);
    gm.saveCurrentState(1);
    EXPECT_TRUE(gm.hasGroupData(1));

    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(1);

    wm.showDialog(std::move(dialog));

    auto* raw = static_cast<GroupDetailsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    // Enter delete confirmation
    raw->handleOption(1);
    EXPECT_TRUE(raw->isConfirmingDelete());

    // Confirm delete (index 3)
    raw->handleOption(3);

    EXPECT_FALSE(raw->isConfirmingDelete());
    EXPECT_FALSE(gm.hasGroupData(1));

    wm.dismissDialog();
}

// 7. Color knob is present and enabled for existing group
TEST_F(GroupDetailsDialogTest, ColorKnobForExistingGroup)
{
    auto& gm = harness.audio().getGroupManager();
    gm.createGroup(0);
    gm.saveCurrentState(0);

    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(0);

    wm.showDialog(std::move(dialog));

    auto* raw = static_cast<GroupDetailsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto knobs = raw->getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[0].id, "groupColor");
    EXPECT_TRUE(knobs[0].isEnabled);

    auto* listModel = std::get_if<Knob::ListModel>(&knobs[0].model);
    ASSERT_NE(listModel, nullptr);
    EXPECT_EQ(listModel->entries.size(), static_cast<std::size_t>(GroupManager::kColorCount));
    EXPECT_EQ(listModel->selectedIndex,
              GroupManager::colorIndex(gm.getGroupColor(0)));

    wm.dismissDialog();
}

// 8. Color knob is disabled for empty group
TEST_F(GroupDetailsDialogTest, ColorKnobDisabledForEmptyGroup)
{
    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(5);  // Empty group

    wm.showDialog(std::move(dialog));

    auto* raw = static_cast<GroupDetailsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    auto knobs = raw->getKnobs(0);
    ASSERT_EQ(knobs.size(), 4u);
    EXPECT_EQ(knobs[0].id, "groupColor");
    EXPECT_FALSE(knobs[0].isEnabled);

    wm.dismissDialog();
}

// 9. Dismiss callback fires on close option
TEST_F(GroupDetailsDialogTest, DismissCallbackFiresOnClose)
{
    bool dismissed = false;

    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(0);
    dialog->setDismissCallback([&dismissed]() { dismissed = true; });

    wm.showDialog(std::move(dialog));

    auto* raw = static_cast<GroupDetailsDialog*>(wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(raw, nullptr);

    // Invoke close (index 0)
    raw->handleOption(0);

    EXPECT_TRUE(dismissed);

    wm.dismissDialog();
}

// 10. Lifecycle -- dialog shows via showDialog and dismisses
TEST_F(GroupDetailsDialogTest, LifecycleShowAndDismiss)
{
    EXPECT_FALSE(wm.hasDialog());

    auto dialog = std::make_unique<GroupDetailsDialog>();
    dialog->setGroupIndex(0);

    wm.showDialog(std::move(dialog));
    EXPECT_TRUE(wm.hasDialog());

    wm.dismissDialog();
    EXPECT_FALSE(wm.hasDialog());
}

TEST_F(GroupDetailsDialogTest, HardwareGroupSelectionCanCreateAndDeleteEmptySlot)
{
    wm.setControllerHost(&controller);
    controller.setWindowManager(&wm);
    wm.initSystemWidgets();

    auto& gm = harness.audio().getGroupManager();
    ASSERT_FALSE(gm.hasGroupData(3));

    // ControllerGestureProcessor sends Select + g4 through this callback.
    controller.notifyGroupSelection(3);
    ASSERT_TRUE(wm.hasDialog());

    auto* dialog = static_cast<GroupDetailsDialog*>(
        wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(dialog, nullptr);
    ASSERT_EQ(dialog->getOptions(0)[1].id, "create");

    wm.handleOptionButton("d2");
    EXPECT_FALSE(wm.hasDialog());
    EXPECT_TRUE(gm.hasGroupData(3));
    EXPECT_TRUE(gm.isGroupActive(3));

    controller.notifyGroupSelection(3);
    ASSERT_TRUE(wm.hasDialog());

    dialog = static_cast<GroupDetailsDialog*>(
        wm.widgetForPanel(DisplaySide::Left));
    ASSERT_NE(dialog, nullptr);
    ASSERT_EQ(dialog->getOptions(0)[1].id, "delete");

    wm.handleOptionButton("d2");
    ASSERT_TRUE(dialog->isConfirmingDelete());
    wm.handleOptionButton("d4");

    EXPECT_FALSE(gm.hasGroupData(3));
    EXPECT_FALSE(gm.isGroupActive(3));
}

} // namespace
