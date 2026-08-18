#include "ListComponent.h"
#include "../theme/UiTheme.h"
#include "../UiRefresh.h"

#include "../../control/ControllerHost.h"
#include "../../control/HardwareConstants.h"

namespace
{
constexpr int kIndentPerLevel = 2;
constexpr uint8_t kNavLedOn = HardwareConstants::kColorWhiteDim;
constexpr uint8_t kNavLedOff = HardwareConstants::kColorOff;

const juce::Colour kBackgroundColour = UiTheme::kBackgroundDark;
const juce::Colour kRowEvenColour   = UiTheme::kBackgroundDark.brighter(0.05f);
const juce::Colour kRowOddColour    = UiTheme::kBackgroundDark.brighter(0.08f);
const juce::Colour kRowDividerColour = UiTheme::kBackgroundDark.darker(0.2f);
const juce::Colour kRowHighlight    = UiTheme::kRowHighlight;
const juce::Colour kTextColour      = juce::Colours::lightgrey;
const juce::Colour kTextHighlight   = juce::Colours::white;

juce::Colour rowColourForIndex(int index, bool isSelected)
{
    if (isSelected)
        return kRowHighlight;

    return (index % 2 == 0) ? kRowEvenColour : kRowOddColour;
}
} // namespace

class ListComponent::Content : public juce::Component
{
public:
    explicit Content(ListComponent& owner)
        : list(owner)
    {
        setInterceptsMouseClicks(true, false);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(kBackgroundColour);

        const int rowHeight = list.getRowHeight();
        const int count = static_cast<int>(list.visibleItems.size());

        for (int index = 0; index < count; ++index)
        {
            auto bounds = juce::Rectangle<int>(0, index * rowHeight, getWidth(), rowHeight);
            const bool isSelected = index == list.selectedIndex;

            list.paintRow(g, bounds, list.visibleItems[static_cast<size_t>(index)], isSelected, index);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        list.grabKeyboardFocus();

        if (list.visibleItems.empty())
            return;

        const int rowHeight = list.getRowHeight();
        const int index = juce::jlimit(0, static_cast<int>(list.visibleItems.size()) - 1,
                                       static_cast<int>(event.position.y / static_cast<float>(rowHeight)));

        list.handleRowClick(index);

        if (event.getNumberOfClicks() > 1)
            list.activateSelection();
    }

private:
    ListComponent& list;
};

ListComponent::ListComponent()
{
    content = std::make_unique<Content>(*this);
    viewport.setViewedComponent(content.get(), false);
    addAndMakeVisible(viewport);

    setWantsKeyboardFocus(true);
}

ListComponent::~ListComponent()
{
    unregisterControllerBindings();
}

void ListComponent::setItems(std::vector<Item> newItems)
{
    rootItems = std::move(newItems);
    rebuildVisibleItems();
    ensureSelectionValid();
    if (selectionChangedCallback != nullptr
        && selectedIndex >= 0
        && selectedIndex < static_cast<int>(visibleItems.size()))
    {
        selectionChangedCallback(*visibleItems[static_cast<size_t>(selectedIndex)].item);
    }
    updateContentSize();
    updateNavIndicators();
    content->repaint();
    repaint();
    requestUiRefresh(*this);
}

const ListComponent::Item* ListComponent::getSelectedItem() const noexcept
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(visibleItems.size()))
        return nullptr;

    return visibleItems[static_cast<size_t>(selectedIndex)].item;
}

void ListComponent::setSelectedId(const juce::String& itemId)
{
    if (itemId.isEmpty())
    {
        setSelectedIndex(-1);
        return;
    }

    for (size_t i = 0; i < visibleItems.size(); ++i)
    {
        if (visibleItems[i].item != nullptr && visibleItems[i].item->id == itemId)
        {
            setSelectedIndex(static_cast<int>(i));
            return;
        }
    }

    setSelectedIndex(-1);
}

juce::String ListComponent::getSelectedId() const noexcept
{
    const auto* item = getSelectedItem();
    return item != nullptr ? item->id : juce::String();
}

void ListComponent::setOnSelectionChanged(std::function<void(Item&)> callback)
{
    selectionChangedCallback = std::move(callback);
}

void ListComponent::setOnSelectionCleared(std::function<void()> callback)
{
    selectionClearedCallback = std::move(callback);
}

void ListComponent::setOnItemActivated(std::function<void(Item&)> callback)
{
    activationCallback = std::move(callback);
}

void ListComponent::setOnItemExpansionChanged(std::function<void(Item&, bool)> callback)
{
    expansionChangedCallback = std::move(callback);
}

void ListComponent::setOnItemPreview(std::function<void(Item&)> callback)
{
    previewCallback = std::move(callback);
}

void ListComponent::setInputManager(InputManager* manager)
{
    if (inputManager == manager)
        return;

    unregisterControllerBindings();
    inputManager = manager;
    registerControllerBindings();
}

void ListComponent::setControllerHost(ControllerHost* host)
{
    controllerHost = host;
    updateNavIndicators();
}

void ListComponent::setEmptyMessage(juce::String message)
{
    if (emptyMessage == message)
        return;
    emptyMessage = std::move(message);
    repaint();
    requestUiRefresh(*this);
}

void ListComponent::setRowHeight(int height)
{
    const int clamped = juce::jmax(20, height);
    if (rowHeight == clamped)
        return;
    rowHeight = clamped;
    updateContentSize();
    repaint();
    requestUiRefresh(*this);
}

void ListComponent::refreshContent()
{
    rebuildVisibleItems();
    ensureSelectionValid();
    if (selectionChangedCallback != nullptr
        && selectedIndex >= 0
        && selectedIndex < static_cast<int>(visibleItems.size()))
    {
        selectionChangedCallback(*visibleItems[static_cast<size_t>(selectedIndex)].item);
    }
    updateContentSize();
    updateNavIndicators();
    content->repaint();
    repaint();
    requestUiRefresh(*this);
}

void ListComponent::invokeSelection()
{
    activateSelection();
}

void ListComponent::setNavigationEnabled(bool enabled)
{
    if (navigationEnabled == enabled)
        return;

    navigationEnabled = enabled;
    updateNavIndicators();
}

void ListComponent::activateNavigation(bool active)
{
    navigationActive = active;
    updateNavIndicators();
}

void ListComponent::clearSelection()
{
    setSelectedIndex(-1);
}

#if MASCHINEPI_TESTS
std::vector<juce::String> ListComponent::visibleIdsForTesting() const
{
    std::vector<juce::String> ids;
    ids.reserve(visibleItems.size());

    for (const auto& visible : visibleItems)
    {
        if (visible.item != nullptr)
            ids.push_back(visible.item->id);
    }

    return ids;
}

int ListComponent::contentHeightForTesting() const
{
    return content != nullptr ? content->getHeight() : 0;
}
#endif

void ListComponent::resized()
{
    viewport.setBounds(getLocalBounds());
    updateContentSize();
}

void ListComponent::paint(juce::Graphics& g)
{
    g.fillAll(kBackgroundColour);

    if (visibleItems.empty())
    {
        g.setColour(kTextColour.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody)));
        g.drawFittedText(emptyMessage,
                         getLocalBounds().reduced(8),
                         juce::Justification::centred,
                         2);
    }
}

void ListComponent::focusGained(FocusChangeType)
{
    navigationActive = true;
    registerControllerBindings();
    if (inputManager != nullptr)
        inputManager->setActiveContext("list", true);
    updateNavIndicators();
}

void ListComponent::focusLost(FocusChangeType)
{
    navigationActive = false;
    if (inputManager != nullptr)
        inputManager->setActiveContext("list", false);
    updateNavIndicators();
}

bool ListComponent::keyPressed(const juce::KeyPress& key)
{
    if (!navigationEnabled)
        return false;

    if (key == juce::KeyPress(juce::KeyPress::upKey))
    {
        moveSelection(-1);
        return true;
    }

    if (key == juce::KeyPress(juce::KeyPress::downKey))
    {
        moveSelection(1);
        return true;
    }

    if (key == juce::KeyPress(juce::KeyPress::leftKey))
    {
        collapseSelection();
        return true;
    }

    if (key == juce::KeyPress(juce::KeyPress::rightKey))
    {
        expandSelection();
        return true;
    }

    if (key == juce::KeyPress(juce::KeyPress::returnKey)
        || key == juce::KeyPress(juce::KeyPress::spaceKey))
    {
        activateSelection();
        return true;
    }

    return false;
}

void ListComponent::rebuildVisibleItems()
{
    visibleItems.clear();

    for (auto& item : rootItems)
        appendVisibleItems(item, nullptr, 0);
}

void ListComponent::appendVisibleItems(Item& item, Item* parent, int depth)
{
    const bool hideItem = item.isVirtualRoot && parent == nullptr;

    if (!hideItem)
        visibleItems.push_back({ &item, parent, depth });

    if (item.isFolder && item.isExpanded)
    {
        const int childDepth = hideItem ? depth : depth + 1;

        for (auto& child : item.children)
            appendVisibleItems(child, &item, childDepth);
    }
}

void ListComponent::ensureSelectionValid()
{
    if (visibleItems.empty())
    {
        selectedIndex = -1;
        return;
    }

    if (selectedIndex >= static_cast<int>(visibleItems.size()))
        selectedIndex = static_cast<int>(visibleItems.size()) - 1;
    else if (selectedIndex < -1)
        selectedIndex = -1;
}

void ListComponent::scrollSelectionIntoView()
{
    if (selectedIndex < 0)
        return;

    if (auto* viewed = viewport.getViewedComponent())
    {
        auto bounds = rowBounds(selectedIndex);
        const int viewHeight = viewport.getViewHeight();
        const int currentY = viewport.getViewPositionY();
        const int viewBottom = currentY + viewHeight;

        int targetY = currentY;

        if (bounds.getY() < currentY)
        {
            targetY = bounds.getY();
        }
        else if (bounds.getBottom() > viewBottom)
        {
            targetY = bounds.getBottom() - viewHeight;
        }

        const int maxY = juce::jmax(0, viewed->getHeight() - viewHeight);
        targetY = juce::jlimit(0, maxY, targetY);

        viewport.setViewPosition(viewport.getViewPositionX(), targetY);
    }
}

void ListComponent::setSelectedIndex(int index, bool notify)
{
    const int previousIndex = selectedIndex;

    if (visibleItems.empty())
    {
        selectedIndex = -1;
    }
    else if (index < 0)
    {
        selectedIndex = -1;
    }
    else
    {
        selectedIndex = juce::jlimit(0, static_cast<int>(visibleItems.size()) - 1, index);
    }

    if (selectedIndex != previousIndex)
    {
        if (notify && selectedIndex >= 0 && selectedIndex < static_cast<int>(visibleItems.size()))
        {
            if (selectionChangedCallback != nullptr)
                selectionChangedCallback(*visibleItems[static_cast<size_t>(selectedIndex)].item);
        }
        else if (notify && selectedIndex < 0)
        {
            if (selectionClearedCallback != nullptr)
                selectionClearedCallback();
        }

        scrollSelectionIntoView();
        updateNavIndicators();
        content->repaint();
        repaint();
        requestUiRefresh(*this);
    }
}

void ListComponent::moveSelection(int delta)
{
    if (visibleItems.empty())
        return;

    const int newIndex = juce::jlimit(0,
                                      static_cast<int>(visibleItems.size()) - 1,
                                      selectedIndex + delta);
    setSelectedIndex(newIndex);
}

void ListComponent::activateSelection()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(visibleItems.size()))
        return;

    auto& visible = visibleItems[static_cast<size_t>(selectedIndex)];
    auto* item = visible.item;

    if (item == nullptr || !item->isSelectable)
        return;

    if (item->isFolder)
    {
        toggleSelectionExpansion();
        return;
    }

    if (activationCallback)
        activationCallback(*item);
}

void ListComponent::expandSelection()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(visibleItems.size()))
        return;

    auto& visible = visibleItems[static_cast<size_t>(selectedIndex)];
    auto* item = visible.item;

    if (item == nullptr)
        return;

    if (!item->isFolder)
    {
        if (previewCallback)
            previewCallback(*item);
        return;
    }

    if (!item->isExpanded)
    {
        item->isExpanded = true;
        if (expansionChangedCallback)
            expansionChangedCallback(*item, true);

        const juce::String currentId = item->id;
        refreshContent();
        setSelectedId(currentId);
        return;
    }

    if (!item->children.empty())
    {
        const juce::String childId = item->children.front().id;
        refreshContent();
        setSelectedId(childId);
    }
}

void ListComponent::collapseSelection()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(visibleItems.size()))
        return;

    auto& visible = visibleItems[static_cast<size_t>(selectedIndex)];
    auto* item = visible.item;

    if (item == nullptr)
        return;

    if (item->isFolder && item->isExpanded)
    {
        item->isExpanded = false;
        if (expansionChangedCallback)
            expansionChangedCallback(*item, false);

        const juce::String currentId = item->id;
        refreshContent();
        setSelectedId(currentId);
        return;
    }

    if (visible.parent != nullptr)
    {
        const juce::String parentId = visible.parent->id;
        refreshContent();
        setSelectedId(parentId);
    }
}

void ListComponent::toggleSelectionExpansion()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(visibleItems.size()))
        return;

    auto& visible = visibleItems[static_cast<size_t>(selectedIndex)];
    auto* item = visible.item;

    if (item == nullptr || !item->isFolder)
        return;

    item->isExpanded = !item->isExpanded;
    if (expansionChangedCallback)
        expansionChangedCallback(*item, item->isExpanded);

    const juce::String id = item->id;
    refreshContent();
    setSelectedId(id);
}

void ListComponent::handleRowClick(int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(visibleItems.size()))
        return;

    setSelectedIndex(rowIndex);
}

void ListComponent::handleControllerNavigation(NavCommand command, const InputEvent& event)
{
    if (!navigationEnabled)
        return;

    // Require explicit activation for navigation (set via activateNavigation or focus events)
    if (!navigationActive)
        return;

    if (const auto* pressedVar = event.metadata.getVarPointer("pressed"))
    {
        const bool pressed = static_cast<bool>(*pressedVar);
        if (!pressed)
            return;
    }

    switch (command)
    {
        case NavCommand::Up:    moveSelection(-1); break;
        case NavCommand::Down:  moveSelection(1);  break;
        case NavCommand::Left:  collapseSelection(); break;
        case NavCommand::Right: expandSelection(); break;
        case NavCommand::Select: activateSelection(); break;
        case NavCommand::Count: break;
    }
}

void ListComponent::registerControllerBindings()
{
    if (inputManager == nullptr)
        return;

    auto registerCommand = [this](NavCommand command, const juce::String& controlId)
    {
        const auto index = static_cast<size_t>(command);
        if (controllerBindingIds[index] != 0)
            return;

        controllerBindingIds[index] = inputManager->addControllerHandler(
            InputManager::HandlerPriority::Component,
            "list",
            controlId,
            [this, command](InputEvent& event)
            {
                handleControllerNavigation(command, event);
            });
    };

    registerCommand(NavCommand::Up,    "nav.up");
    registerCommand(NavCommand::Down,  "nav.down");
    registerCommand(NavCommand::Left,  "nav.left");
    registerCommand(NavCommand::Right, "nav.right");
    registerCommand(NavCommand::Select,"nav.select");
}

void ListComponent::unregisterControllerBindings()
{
    if (inputManager == nullptr)
        return;

    for (auto& bindingId : controllerBindingIds)
    {
        if (bindingId != 0)
        {
            inputManager->removeHandler(bindingId);
            bindingId = 0;
        }
    }
}

void ListComponent::updateNavIndicators()
{
#if MASCHINEPI_TESTS
    juce::ignoreUnused(controllerHost);
    return;
#else
    if (controllerHost == nullptr)
        return;

    auto setLed = [this](const std::string& name, bool enabled)
    {
        controllerHost->setNavigationLed(name, enabled ? kNavLedOn : kNavLedOff);
    };

    // Require explicit activation for LED indicators (set via activateNavigation or focus events)
    const bool shouldShowLeds = navigationEnabled && navigationActive && selectedIndex >= 0 && !visibleItems.empty();

    if (!shouldShowLeds)
    {
        setLed("navUp", false);
        setLed("navDown", false);
        setLed("navLeft", false);
        setLed("navRight", false);
        return;
    }

    const bool canMoveUp = selectedIndex > 0;
    const bool canMoveDown = selectedIndex < static_cast<int>(visibleItems.size()) - 1;

    bool canMoveLeft = false;
    bool canMoveRight = false;

    const auto& visible = visibleItems[static_cast<size_t>(selectedIndex)];
    const Item* item = visible.item;

    if (item != nullptr)
    {
        if (item->isFolder)
        {
            canMoveRight = true;
            canMoveLeft = item->isExpanded || (visible.parent != nullptr);
        }
        else
        {
            canMoveRight = false;
            canMoveLeft = (visible.parent != nullptr);
        }
    }

    setLed("navUp", canMoveUp);
    setLed("navDown", canMoveDown);
    setLed("navLeft", canMoveLeft);
    setLed("navRight", canMoveRight);
#endif
}

juce::Rectangle<int> ListComponent::rowBounds(int rowIndex) const
{
    return juce::Rectangle<int>(0,
                                rowIndex * rowHeight,
                                content != nullptr ? content->getWidth() : getWidth(),
                                rowHeight);
}

void ListComponent::paintRow(juce::Graphics& g,
                             const juce::Rectangle<int>& bounds,
                             const VisibleItem& item,
                             bool isSelected,
                             int rowIndex) const
{

    g.setColour(rowColourForIndex(rowIndex, isSelected));
    g.fillRect(bounds);

    g.setColour(kRowDividerColour);
    g.fillRect(bounds.withHeight(1).withY(bounds.getBottom() - 1));

    auto contentArea = bounds.reduced(12, 2);
    const int indent = juce::jmin(item.depth * kIndentPerLevel, 5);
    contentArea.removeFromLeft(indent);

    if (item.item != nullptr && item.item->isFolder)
    {
        auto iconArea = contentArea.removeFromLeft(12).withTrimmedLeft(-2).withTrimmedRight(2);
        iconArea = iconArea.withSizeKeepingCentre(10, 10);

        juce::Path chevron;
        if (item.item->isExpanded)
        {
            chevron.startNewSubPath(static_cast<float>(iconArea.getX()),
                                    static_cast<float>(iconArea.getY()) + 3.0f);
            chevron.lineTo(static_cast<float>(iconArea.getCentreX()),
                           static_cast<float>(iconArea.getBottom()) - 2.0f);
            chevron.lineTo(static_cast<float>(iconArea.getRight()),
                           static_cast<float>(iconArea.getY()) + 3.0f);
        }
        else
        {
            chevron.startNewSubPath(static_cast<float>(iconArea.getX()) + 2.0f,
                                    static_cast<float>(iconArea.getY()));
            chevron.lineTo(static_cast<float>(iconArea.getRight()) - 2.0f,
                           static_cast<float>(iconArea.getCentreY()));
            chevron.lineTo(static_cast<float>(iconArea.getX()) + 2.0f,
                           static_cast<float>(iconArea.getBottom()));
        }

        g.setColour(kTextColour);
        g.strokePath(chevron, juce::PathStrokeType(1.6f));
    }

    if (item.item == nullptr)
        return;

    auto labelArea = contentArea;
    juce::Rectangle<int> descriptionArea;

    if (item.item->description.isNotEmpty())
    {
        const int descWidth = juce::jmin(200, labelArea.getWidth() / 2);
        descriptionArea = labelArea.removeFromRight(descWidth).reduced(4, 0);
    }

    const bool isAncestor = isAncestorOfSelection(item.item);
    const bool shouldDim = isAncestor && !isSelected;

    auto labelColour = isSelected ? kTextHighlight : kTextColour;
    if (shouldDim)
        labelColour = labelColour.withMultipliedAlpha(0.6f);

    g.setColour(labelColour);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBody, juce::Font::bold)));
    g.drawFittedText(item.item->label,
                     labelArea,
                     juce::Justification::centredLeft,
                     1);

    if (descriptionArea.isEmpty() || item.item->description.isEmpty())
        return;

    auto descriptionColour = kTextColour.withAlpha(0.6f);
    if (shouldDim)
        descriptionColour = descriptionColour.withMultipliedAlpha(0.7f);
    g.setColour(descriptionColour);
    g.setFont(juce::Font(juce::FontOptions(UiTheme::Fonts::kBodySmall)));
    g.drawFittedText(item.item->description,
                     descriptionArea,
                     juce::Justification::centredRight,
                     1);
}

void ListComponent::updateContentSize()
{
    if (content == nullptr)
        return;

    const int viewX = viewport.getViewPositionX();
    const int viewY = viewport.getViewPositionY();
    const int viewWidth = viewport.getViewWidth();
    const int viewHeight = viewport.getViewHeight();

    const int contentHeight = juce::jmax(rowHeight * static_cast<int>(visibleItems.size()),
                                         rowHeight);

    content->setSize(juce::jmax(viewWidth, viewport.getWidth()), contentHeight);

    const int maxX = juce::jmax(0, content->getWidth() - viewWidth);
    const int maxY = juce::jmax(0, contentHeight - viewHeight);

    viewport.setViewPosition(juce::jlimit(0, maxX, viewX),
                             juce::jlimit(0, maxY, viewY));

    scrollSelectionIntoView();
    repaint();
    requestUiRefresh(*this);
}

bool ListComponent::isAncestorOfSelection(const Item* candidate) const
{
    if (candidate == nullptr || selectedIndex < 0 || selectedIndex >= static_cast<int>(visibleItems.size()))
        return false;

    const Item* current = visibleItems[static_cast<size_t>(selectedIndex)].parent;

    while (current != nullptr)
    {
        if (current == candidate)
            return true;

        current = findParentOf(current);
    }

    return false;
}

const ListComponent::Item* ListComponent::findParentOf(const Item* item) const
{
    for (const auto& visible : visibleItems)
    {
        if (visible.item == item)
            return visible.parent;
    }

    return nullptr;
}
