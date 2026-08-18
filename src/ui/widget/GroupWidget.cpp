#include "GroupWidget.h"

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"
#include "../../engine/AudioEngine.h"
#include "../../engine/GroupManager.h"
#include "../hw/HardwareState.h"
#include "../theme/UiTheme.h"

WidgetDescriptor GroupWidget::describe() const
{
    return { "groups", 0, false, DisplayConstraint::Any };
}

void GroupWidget::onActivated(int panelOffset)
{
    panelOffset_ = panelOffset;
    updateLeds();
}

void GroupWidget::onDeactivated()
{
}

std::vector<std::string> GroupWidget::requiredResources(int /*page*/)
{
    return { "g1", "g2", "g3", "g4", "g5", "g6", "g7", "g8" };
}

void GroupWidget::handleButton(const controller_events::ButtonEvent& e)
{
    if (!e.pressed)
        return;

    // Parse g1-g8 button names
    if (e.name.size() != 2 || e.name[0] != 'g')
        return;

    int groupNum = e.name[1] - '0';
    if (groupNum < 1 || groupNum > 8)
        return;

    int groupIndex = groupNum - 1;

    // Select+group opens group details dialog. The LED overlay on
    // `updateLeds` already dim-whites groups while select is held — pairing
    // the modifier keeps the "Select is the group-picker modifier" story
    // consistent across lighting and action.
    const bool selectHeld = controllerHost() != nullptr
                         && controllerHost()->isSelectPressed();
    if (selectHeld && groupDetailsCallback_)
    {
        groupDetailsCallback_(groupIndex);
        return;
    }

    auto& groupManager = engine().getGroupManager();

    if (groupManager.isGroupActive(groupIndex) || groupManager.hasGroupData(groupIndex))
    {
        groupManager.recallGroup(groupIndex);
        updateLeds();
    }
}

void GroupWidget::updateLeds()
{
    auto& hardware = hw();
    const auto id = describe().id.toStdString();
    auto& groupManager = engine().getGroupManager();

    const bool selectActive = controllerHost() != nullptr && controllerHost()->isSelectPressed();
    constexpr uint8_t kSelectDimWhite = HardwareConstants::kColorWhiteDim;

    for (int i = 0; i < 8; ++i)
    {
        const std::string ledName = "g" + std::to_string(i + 1);
        uint8_t color = 0;

        if (groupManager.isGroupActive(i))
        {
            color = UiTheme::brightestHueVariant(groupManager.getGroupColor(i));
        }
        else if (groupManager.hasGroupData(i))
        {
            color = UiTheme::dimmestHueVariant(groupManager.getGroupColor(i));
        }
        else if (selectActive)
        {
            color = kSelectDimWhite;
        }
        // Empty group: off (color stays 0)

        hardware.setLed(ledName, color, id);
    }
}
