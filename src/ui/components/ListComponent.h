#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../input/InputManager.h"
#include "../../input/InputEvent.h"

class ControllerHost;

class ListComponent : public juce::Component
{
public:
    struct Item
    {
        juce::String id;
        juce::String label;
        bool isFolder { false };
        bool isExpanded { false };
        bool isSelectable { true };
        bool isVirtualRoot { false };
        juce::String description;
        std::vector<Item> children;
    };

    ListComponent();
    ~ListComponent() override;

    void setItems(std::vector<Item> newItems);
    std::vector<Item>& getItems() noexcept { return rootItems; }
    const std::vector<Item>& getItems() const noexcept { return rootItems; }

    const Item* getSelectedItem() const noexcept;
    void setSelectedId(const juce::String& itemId);
    juce::String getSelectedId() const noexcept;

    void setOnSelectionChanged(std::function<void(Item&)> callback);
    void setOnSelectionCleared(std::function<void()> callback);
    void setOnItemActivated(std::function<void(Item&)> callback);
    void setOnItemExpansionChanged(std::function<void(Item&, bool)> callback);
    void setOnItemPreview(std::function<void(Item&)> callback);

    void setInputManager(InputManager* manager);
    void setControllerHost(ControllerHost* host);

    void setEmptyMessage(juce::String message);
    void setRowHeight(int height);

    void refreshContent();

    void setNavigationEnabled(bool enabled);
    bool isNavigationEnabled() const noexcept { return navigationEnabled; }

    // Screen-based navigation activation (bypasses focus requirement)
    void activateNavigation(bool active);

    void clearSelection();

#if MASCHINEPI_TESTS
    std::vector<juce::String> visibleIdsForTesting() const;
    int contentHeightForTesting() const;
#endif

    void invokeSelection();
    void moveSelection(int delta);
    void expandSelection();
    void collapseSelection();

    void resized() override;
    void paint(juce::Graphics& g) override;

    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

private:
    class Content;
    friend class Content;

    struct VisibleItem
    {
        Item* item { nullptr };
        Item* parent { nullptr };
        int depth { 0 };
    };

    enum class NavCommand
    {
        Up = 0,
        Down,
        Left,
        Right,
        Select,
        Count
    };

    bool keyPressed(const juce::KeyPress& key) override;

    void rebuildVisibleItems();
    void appendVisibleItems(Item& item, Item* parent, int depth);
    void ensureSelectionValid();
    void scrollSelectionIntoView();
    void setSelectedIndex(int index, bool notify = true);
    void activateSelection();
    void toggleSelectionExpansion();
    void handleRowClick(int rowIndex);
    void handleControllerNavigation(NavCommand command, const InputEvent& event);

    void registerControllerBindings();
    void unregisterControllerBindings();
    void updateNavIndicators();

    int getRowHeight() const noexcept { return rowHeight; }
    juce::Rectangle<int> rowBounds(int rowIndex) const;
    void paintRow(juce::Graphics& g,
                  const juce::Rectangle<int>& bounds,
                  const VisibleItem& item,
                  bool isSelected,
                  int rowIndex) const;

    void updateContentSize();
    bool isAncestorOfSelection(const Item* candidate) const;
    const Item* findParentOf(const Item* item) const;

    InputManager* inputManager { nullptr };
    ControllerHost* controllerHost { nullptr };

    std::vector<Item> rootItems;
    std::vector<VisibleItem> visibleItems;

    juce::Viewport viewport;
    std::unique_ptr<Content> content;

    std::function<void(Item&)> selectionChangedCallback;
    std::function<void(Item&)> activationCallback;
    std::function<void(Item&, bool)> expansionChangedCallback;
    std::function<void(Item&)> previewCallback;
    std::function<void()> selectionClearedCallback;

    std::array<InputManager::BindingId, static_cast<size_t>(NavCommand::Count)> controllerBindingIds {};

    int rowHeight { 28 };
    int selectedIndex { -1 };
    bool navigationEnabled { true };
    bool navigationActive { false };
    juce::String emptyMessage { "No items available" };
};
