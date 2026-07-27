#include "../../Source/AccessClass.h"
#include "../../Source/Processors/Editors/SyncLineSelector.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "gtest/gtest.h"

namespace
{
class TestSyncLineListener final : public SyncLineSelector::Listener
{
public:
    void selectedLineChanged (int newSelectedLine) override
    {
        selectedLine = newSelectedLine;
    }

    int getSelectedLine() override
    {
        return selectedLine;
    }

    void primaryStreamChanged() override
    {
        primary = true;
    }

    bool isPrimaryStream() override
    {
        return primary;
    }

    int selectedLine = 1;
    bool primary = false;
};

Component* findDescendantById (Component& parent, const String& componentId)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == componentId)
            return child;

        if (auto* descendant = findDescendantById (*child, componentId))
            return descendant;
    }

    return nullptr;
}

class SyncLineSelectorAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::deleteInstance();
        MessageManager::getInstance();
        AccessClass::clearAccessClassStateForTesting();
        processorGraph = std::make_unique<ProcessorGraph> (true);
    }

    void TearDown() override
    {
        processorGraph.reset();
        AccessClass::clearAccessClassStateForTesting();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

private:
    std::unique_ptr<ProcessorGraph> processorGraph;
};
} // namespace

TEST_F (SyncLineSelectorAccessibilityTests, ExposesLineChoicesAndPrimaryClockAction)
{
    Component parent;
    TestSyncLineListener listener;
    SyncLineSelector selector (&parent, &listener, 4, listener.selectedLine, false, true);

    EXPECT_EQ (selector.getComponentID(), "oe.popup.sync_line");
    EXPECT_EQ (selector.getTitle(), "Line selector");
    EXPECT_EQ (selector.getDescription(), "Choose a TTL or synchronization line.");
    EXPECT_TRUE (selector.isAccessible());

    ASSERT_EQ (selector.buttons.size(), 4);
    for (int index = 0; index < selector.buttons.size(); ++index)
    {
        auto* button = selector.buttons[index];
        const auto lineNumber = index + 1;
        const auto selected = index == listener.selectedLine;
        EXPECT_EQ (button->getComponentID(),
                   "oe.popup.sync_line.line_" + String (lineNumber));
        EXPECT_EQ (button->getTitle(),
                   "Line " + String (lineNumber) + (selected ? " (selected)" : ""));
        EXPECT_EQ (button->getDescription(),
                   selected ? "Selected line " + String (lineNumber) + "."
                            : "Select line " + String (lineNumber) + ".");
        EXPECT_TRUE (button->isAccessible());
    }

    auto* primary = findDescendantById (selector, "oe.popup.sync_line.set_primary");
    ASSERT_NE (primary, nullptr);
    EXPECT_EQ (primary->getTitle(), "Set as main clock");
    EXPECT_EQ (primary->getDescription(),
               "Use this stream as the main synchronization clock.");
    auto primaryHandler = primary->createAccessibilityHandler();
    ASSERT_NE (primaryHandler, nullptr);
    EXPECT_EQ (primaryHandler->getRole(), AccessibilityRole::button);

    selector.buttonClicked (selector.buttons[2]);
    EXPECT_EQ (listener.selectedLine, 2);
    EXPECT_EQ (selector.getSelectedChannel(), 2);

    selector.buttonClicked (dynamic_cast<Button*> (primary));
    EXPECT_TRUE (listener.primary);
}

TEST_F (SyncLineSelectorAccessibilityTests, RefreshesThePublishedSelectedLine)
{
    Component parent;
    TestSyncLineListener listener;
    SyncLineSelector selector (&parent, &listener, 4, listener.selectedLine, true, true);

    listener.selectedLine = 3;
    selector.updatePopup();

    EXPECT_EQ (selector.buttons[1]->getTitle(), "Line 2");
    EXPECT_EQ (selector.buttons[1]->getDescription(), "Select line 2.");
    EXPECT_EQ (selector.buttons[3]->getTitle(), "Line 4 (selected)");
    EXPECT_EQ (selector.buttons[3]->getDescription(), "Selected line 4.");
}
