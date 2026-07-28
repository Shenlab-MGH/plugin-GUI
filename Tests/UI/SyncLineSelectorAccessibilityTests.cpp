#include "../../Source/AccessClass.h"
#include "../../Source/Processors/Editors/SyncLineSelector.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/UI/SemanticComponent.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

namespace
{
class TestSyncLineListener final : public SyncLineSelector::Listener
{
public:
    void selectedLineChanged (int newSelectedLine) override
    {
        lineChangeUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        selectedLine = newSelectedLine;
        ++lineChangeCount;
        if (onLineChange)
            onLineChange();
    }

    int getSelectedLine() override
    {
        return selectedLine;
    }

    void primaryStreamChanged() override
    {
        primaryChangeUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        primary = true;
        ++primaryChangeCount;
        if (onPrimaryChange)
            onPrimaryChange();
    }

    bool isPrimaryStream() override
    {
        return primary;
    }

    int selectedLine = 1;
    bool primary = false;
    int lineChangeCount = 0;
    int primaryChangeCount = 0;
    std::atomic<bool> lineChangeUsedMessageThread { false };
    std::atomic<bool> primaryChangeUsedMessageThread { false };
    std::function<void()> onLineChange;
    std::function<void()> onPrimaryChange;
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

bool invokeFromWorker (
    const AccessibilityActions& actions,
    AccessibilityActionType action)
{
    std::atomic<bool> workerEntered { false };
    std::atomic<bool> invoked { false };
    std::thread worker (
        [&]
        {
            workerEntered.store (true);
            invoked.store (actions.invoke (action));
        });

    while (! workerEntered.load())
        std::this_thread::yield();

    auto* messageManager = MessageManager::getInstance();
    const auto timeoutAt =
        Time::getMillisecondCounterHiRes() + 5000.0;
    bool timedOut = false;
    while (! invoked.load())
    {
        messageManager->runDispatchLoopUntil (10);
        if (! timedOut
            && Time::getMillisecondCounterHiRes()
                   >= timeoutAt)
        {
            ADD_FAILURE()
                << "Worker UIA action exceeded five seconds";
            timedOut = true;
        }
    }

    worker.join();
    return invoked.load();
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
    applySemanticMetadata (
        parent,
        "oe.processor.100.parameter.sync_line",
        "Synchronization line",
        "Choose the synchronization line.");
    TestSyncLineListener listener;
    SyncLineSelector selector (&parent, &listener, 4, listener.selectedLine, false, true);

    EXPECT_EQ (selector.getComponentID(),
               "oe.processor.100.parameter.sync_line.popup");
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
                   "oe.processor.100.parameter.sync_line.popup.line_"
                       + String (lineNumber));
        EXPECT_EQ (button->getTitle(), "Line " + String (lineNumber));
        EXPECT_EQ (button->getDescription(),
                   selected ? "Selected line " + String (lineNumber) + "."
                            : "Select line " + String (lineNumber) + ".");
        EXPECT_TRUE (button->isAccessible());

        auto handler = button->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (handler->getRole(), AccessibilityRole::toggleButton);
        EXPECT_TRUE (handler->getActions().contains (
            AccessibilityActionType::toggle));
        EXPECT_FALSE (handler->getActions().contains (
            AccessibilityActionType::press));
        EXPECT_EQ (handler->getCurrentState().isChecked(), selected);
        EXPECT_EQ (handler->getCurrentState().isSelected(), selected);
        ASSERT_NE (handler->getValueInterface(), nullptr);
        EXPECT_EQ (
            handler->getValueInterface()->getCurrentValueAsString(),
            selected ? "Selected" : "Not selected");
    }

    auto* primary = findDescendantById (
        selector,
        "oe.processor.100.parameter.sync_line.popup.set_primary");
    ASSERT_NE (primary, nullptr);
    EXPECT_EQ (primary->getTitle(), "Set as main clock");
    EXPECT_EQ (primary->getDescription(),
               "Use this stream as the main synchronization clock.");
    auto primaryHandler = primary->createAccessibilityHandler();
    ASSERT_NE (primaryHandler, nullptr);
    EXPECT_EQ (primaryHandler->getRole(), AccessibilityRole::button);
    EXPECT_TRUE (primaryHandler->getActions().contains (
        AccessibilityActionType::press));
    EXPECT_FALSE (primaryHandler->getActions().contains (
        AccessibilityActionType::toggle));

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
    auto formerHandler =
        selector.buttons[1]->createAccessibilityHandler();
    auto selectedHandler =
        selector.buttons[3]->createAccessibilityHandler();
    ASSERT_NE (formerHandler, nullptr);
    ASSERT_NE (selectedHandler, nullptr);

    listener.selectedLine = 3;
    selector.updatePopup();

    EXPECT_EQ (selector.buttons[1]->getTitle(), "Line 2");
    EXPECT_EQ (selector.buttons[1]->getDescription(), "Select line 2.");
    EXPECT_EQ (selector.buttons[3]->getTitle(), "Line 4");
    EXPECT_EQ (selector.buttons[3]->getDescription(), "Selected line 4.");

    EXPECT_FALSE (formerHandler->getCurrentState().isChecked());
    EXPECT_FALSE (formerHandler->getCurrentState().isSelected());
    EXPECT_EQ (
        formerHandler->getValueInterface()->getCurrentValueAsString(),
        "Not selected");
    EXPECT_TRUE (selectedHandler->getCurrentState().isChecked());
    EXPECT_TRUE (selectedHandler->getCurrentState().isSelected());
    EXPECT_EQ (
        selectedHandler->getValueInterface()->getCurrentValueAsString(),
        "Selected");
}

TEST_F (SyncLineSelectorAccessibilityTests, NonOptionalLinesExposeRadioSemantics)
{
    Component parent;
    TestSyncLineListener listener;
    SyncLineSelector selector (
        &parent,
        &listener,
        4,
        listener.selectedLine,
        true,
        false);

    auto handler =
        selector.buttons[0]->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::radioButton);
    EXPECT_TRUE (handler->getActions().contains (
        AccessibilityActionType::press));
    EXPECT_FALSE (handler->getActions().contains (
        AccessibilityActionType::toggle));
}

TEST_F (SyncLineSelectorAccessibilityTests, StandalonePrimaryButtonRetainsDefaultPress)
{
    SetPrimaryButton button (
        "Set as main clock");
    int clickCount = 0;
    bool clickUsedMessageThread = false;
    button.onClick =
        [&]
        {
            ++clickCount;
            clickUsedMessageThread =
                MessageManager::getInstance()
                    ->isThisTheMessageThread();
        };
    auto handler =
        button.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    String workerTitle;
    std::thread reader (
        [&]
        {
            workerTitle =
                handler->getTitle();
        });
    reader.join();
    EXPECT_EQ (
        workerTitle,
        "Set as main clock");

    ASSERT_TRUE (invokeFromWorker (
        handler->getActions(),
        AccessibilityActionType::press));
    for (int attempt = 0;
         attempt < 20
             && clickCount == 0;
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }

    EXPECT_EQ (clickCount, 1);
    EXPECT_TRUE (clickUsedMessageThread);
}

TEST_F (SyncLineSelectorAccessibilityTests, WorkerActionsUseTheMessageThread)
{
    Component parent;
    TestSyncLineListener listener;
    SyncLineSelector selector (
        &parent,
        &listener,
        4,
        listener.selectedLine,
        false,
        true);

    auto lineHandler =
        selector.buttons[2]->createAccessibilityHandler();
    ASSERT_NE (lineHandler, nullptr);
    ASSERT_TRUE (invokeFromWorker (
        lineHandler->getActions(),
        AccessibilityActionType::toggle));
    EXPECT_EQ (listener.selectedLine, 2);
    EXPECT_EQ (listener.lineChangeCount, 1);
    EXPECT_TRUE (listener.lineChangeUsedMessageThread.load());
    EXPECT_TRUE (lineHandler->getCurrentState().isChecked());
    EXPECT_EQ (
        lineHandler->getDescription(),
        "Selected line 3.");
    EXPECT_EQ (
        lineHandler->getHelp(),
        "Selected line 3.");

    auto* primary = findDescendantById (
        selector,
        "oe.popup.sync_line.set_primary");
    ASSERT_NE (primary, nullptr);
    auto primaryHandler = primary->createAccessibilityHandler();
    ASSERT_NE (primaryHandler, nullptr);

    ASSERT_TRUE (invokeFromWorker (
        lineHandler->getActions(),
        AccessibilityActionType::toggle));
    EXPECT_EQ (listener.selectedLine, -1);
    EXPECT_EQ (listener.lineChangeCount, 2);
    EXPECT_FALSE (lineHandler->getCurrentState().isChecked());
    EXPECT_EQ (
        lineHandler->getDescription(),
        "Select line 3.");
    EXPECT_EQ (
        lineHandler->getHelp(),
        "Select line 3.");
    EXPECT_FALSE (primary->isEnabled());

    ASSERT_TRUE (invokeFromWorker (
        lineHandler->getActions(),
        AccessibilityActionType::toggle));
    EXPECT_EQ (listener.selectedLine, 2);
    EXPECT_EQ (listener.lineChangeCount, 3);
    EXPECT_TRUE (primary->isEnabled());

    ASSERT_TRUE (invokeFromWorker (
        primaryHandler->getActions(),
        AccessibilityActionType::press));
    EXPECT_EQ (listener.primaryChangeCount, 1);
    EXPECT_TRUE (listener.primaryChangeUsedMessageThread.load());
}

TEST_F (SyncLineSelectorAccessibilityTests, ProvidersCanBeReadFromAWorker)
{
    Component parent;
    TestSyncLineListener listener;
    SyncLineSelector selector (
        &parent,
        &listener,
        4,
        listener.selectedLine,
        false,
        true);
    auto lineHandler =
        selector.buttons[1]->createAccessibilityHandler();
    auto* primary = findDescendantById (
        selector,
        "oe.popup.sync_line.set_primary");
    ASSERT_NE (lineHandler, nullptr);
    ASSERT_NE (primary, nullptr);
    auto primaryHandler =
        primary->createAccessibilityHandler();
    ASSERT_NE (primaryHandler, nullptr);

    String lineTitle;
    String lineDescription;
    String lineHelp;
    String lineValue;
    AccessibleState lineState;
    bool lineEnabled = false;
    String primaryTitle;
    String primaryDescription;
    String primaryHelp;
    bool primaryEnabled = false;
    std::thread reader (
        [&]
        {
            lineTitle = lineHandler->getTitle();
            lineDescription =
                lineHandler->getDescription();
            lineHelp = lineHandler->getHelp();
            lineValue =
                lineHandler->getValueInterface()
                    ->getCurrentValueAsString();
            lineState =
                lineHandler->getCurrentState();
            lineEnabled =
                lineHandler->isEnabled();
            primaryTitle =
                primaryHandler->getTitle();
            primaryDescription =
                primaryHandler->getDescription();
            primaryHelp =
                primaryHandler->getHelp();
            primaryEnabled =
                primaryHandler->isEnabled();
        });
    reader.join();

    EXPECT_EQ (lineTitle, "Line 2");
    EXPECT_EQ (
        lineDescription,
        "Selected line 2.");
    EXPECT_EQ (lineHelp, lineDescription);
    EXPECT_EQ (lineValue, "Selected");
    EXPECT_TRUE (lineState.isChecked());
    EXPECT_TRUE (lineState.isSelected());
    EXPECT_TRUE (lineEnabled);
    EXPECT_EQ (
        primaryTitle,
        "Set as main clock");
    EXPECT_EQ (
        primaryDescription,
        "Use this stream as the main synchronization clock.");
    EXPECT_EQ (
        primaryHelp,
        primaryDescription);
    EXPECT_TRUE (primaryEnabled);
}

TEST_F (SyncLineSelectorAccessibilityTests, DisabledAndStaleActionsAreNoOps)
{
    Component parent;
    TestSyncLineListener listener;
    auto selector =
        std::make_unique<SyncLineSelector> (
            &parent,
            &listener,
            4,
            listener.selectedLine,
            false,
            true);

    auto lineHandler =
        selector->buttons[2]->createAccessibilityHandler();
    ASSERT_NE (lineHandler, nullptr);
    const auto lineActions = lineHandler->getActions();
    selector->buttons[2]->setEnabled (false);
    EXPECT_TRUE (invokeFromWorker (
        lineActions,
        AccessibilityActionType::toggle));
    EXPECT_EQ (listener.lineChangeCount, 0);
    EXPECT_EQ (listener.selectedLine, 1);

    auto* primary = findDescendantById (
        *selector,
        "oe.popup.sync_line.set_primary");
    ASSERT_NE (primary, nullptr);
    auto primaryHandler =
        primary->createAccessibilityHandler();
    ASSERT_NE (primaryHandler, nullptr);
    const auto primaryActions =
        primaryHandler->getActions();
    primary->setEnabled (false);
    EXPECT_TRUE (invokeFromWorker (
        primaryActions,
        AccessibilityActionType::press));
    EXPECT_EQ (listener.primaryChangeCount, 0);

    lineHandler.reset();
    primaryHandler.reset();
    selector.reset();
    EXPECT_TRUE (invokeFromWorker (
        lineActions,
        AccessibilityActionType::toggle));
    EXPECT_TRUE (invokeFromWorker (
        primaryActions,
        AccessibilityActionType::press));
    EXPECT_EQ (listener.lineChangeCount, 0);
    EXPECT_EQ (listener.primaryChangeCount, 0);
}

TEST_F (SyncLineSelectorAccessibilityTests, ListenerCanDestroyPopupDuringLineAction)
{
    Component parent;
    TestSyncLineListener listener;
    auto selector =
        std::make_unique<SyncLineSelector> (
            &parent,
            &listener,
            4,
            listener.selectedLine,
            false,
            true);
    auto handler =
        selector->buttons[2]->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions = handler->getActions();
    handler.reset();
    listener.onLineChange =
        [&selector]
        {
            selector.reset();
        };

    EXPECT_TRUE (invokeFromWorker (
        actions,
        AccessibilityActionType::toggle));
    EXPECT_EQ (selector, nullptr);
    EXPECT_EQ (listener.lineChangeCount, 1);
    EXPECT_TRUE (listener.lineChangeUsedMessageThread.load());

    listener.onLineChange = {};
    EXPECT_TRUE (invokeFromWorker (
        actions,
        AccessibilityActionType::toggle));
    EXPECT_EQ (listener.lineChangeCount, 1);
}

TEST_F (SyncLineSelectorAccessibilityTests, ListenerCanDestroyPopupDuringPrimaryAction)
{
    Component parent;
    TestSyncLineListener listener;
    auto selector =
        std::make_unique<SyncLineSelector> (
            &parent,
            &listener,
            4,
            listener.selectedLine,
            false,
            true);
    auto* primary = findDescendantById (
        *selector,
        "oe.popup.sync_line.set_primary");
    ASSERT_NE (primary, nullptr);
    auto handler =
        primary->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions = handler->getActions();
    handler.reset();
    listener.onPrimaryChange =
        [&selector]
        {
            selector.reset();
        };

    EXPECT_TRUE (invokeFromWorker (
        actions,
        AccessibilityActionType::press));
    EXPECT_EQ (selector, nullptr);
    EXPECT_EQ (listener.primaryChangeCount, 1);
    EXPECT_TRUE (
        listener.primaryChangeUsedMessageThread.load());

    listener.onPrimaryChange = {};
    EXPECT_TRUE (invokeFromWorker (
        actions,
        AccessibilityActionType::press));
    EXPECT_EQ (listener.primaryChangeCount, 1);
}

TEST_F (SyncLineSelectorAccessibilityTests, ExternalValidLineReenablesPrimaryAction)
{
    Component parent;
    TestSyncLineListener listener;
    listener.selectedLine = -1;
    SyncLineSelector selector (
        &parent,
        &listener,
        4,
        listener.selectedLine,
        false,
        true);
    auto* primary = findDescendantById (
        selector,
        "oe.popup.sync_line.set_primary");
    ASSERT_NE (primary, nullptr);
    EXPECT_FALSE (primary->isEnabled());

    listener.selectedLine = 2;
    selector.updatePopup();

    EXPECT_TRUE (primary->isEnabled());
    auto selectedHandler =
        selector.buttons[2]->createAccessibilityHandler();
    ASSERT_NE (selectedHandler, nullptr);
    EXPECT_TRUE (
        selectedHandler->getCurrentState().isChecked());
    EXPECT_EQ (
        selectedHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Selected");
}
