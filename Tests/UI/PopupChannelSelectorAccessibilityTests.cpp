#include "../../Source/AccessClass.h"
#include "../../Source/Processors/Editors/PopupChannelSelector.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/UI/SemanticComponent.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

namespace
{
class TestChannelListener final : public PopupChannelSelector::Listener
{
public:
    Array<int> getSelectedChannels() override { return selectedChannels; }

    void channelStateChanged (Array<int> selected) override
    {
        changeUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        selectedChannels = std::move (selected);
        ++changeCount;
        if (onChange)
            onChange();
    }

    int getChannelCount() override { return channelCount; }

    int channelCount = 10;
    Array<int> selectedChannels { 0 };
    int changeCount = 0;
    std::atomic<bool>
        changeUsedMessageThread { false };
    std::function<void()> onChange;
};

Component* findDescendantById (Component& parent, const String& id)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* descendant = findDescendantById (*child, id))
            return descendant;
    }

    return nullptr;
}

bool invokeFromWorker (
    const AccessibilityActions& actions,
    AccessibilityActionType action)
{
    std::atomic<bool> workerEntered {
        false
    };
    std::atomic<bool> invoked { false };
    std::thread worker (
        [&]
        {
            workerEntered.store (true);
            invoked.store (
                actions.invoke (action));
        });
    while (! workerEntered.load())
        std::this_thread::yield();

    auto* messageManager =
        MessageManager::getInstance();
    const auto timeoutAt =
        Time::getMillisecondCounterHiRes()
        + 5000.0;
    bool timedOut = false;
    while (! invoked.load())
    {
        messageManager
            ->runDispatchLoopUntil (
                10);
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

class PopupChannelSelectorAccessibilityTests : public ::testing::Test
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

    static std::vector<bool> initialStates()
    {
        return { true, false, false, false, false,
                 false, false, false, false, false };
    }

    static Array<String> channelNames()
    {
        return { "AP1", "AP2", "AP3", "AP4", "AP5",
                 "AP6", "AP7", "AP8", "AP9", "AP10" };
    }

private:
    std::unique_ptr<ProcessorGraph> processorGraph;
};
} // namespace

TEST_F (PopupChannelSelectorAccessibilityTests, PublishesEverySelectionControl)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    PopupChannelSelector selector (
        &anchor,
        &listener,
        initialStates(),
        channelNames(),
        "Recorded channels");
    const String prefix = "oe.processor.100.parameter.channels.popup";

    EXPECT_EQ (selector.getComponentID(), prefix);
    EXPECT_EQ (selector.getTitle(), "Recorded channels");
    EXPECT_EQ (selector.getDescription(),
               "Select or deselect channels.");
    EXPECT_TRUE (selector.isAccessible());

    auto* viewport = findDescendantById (selector, prefix + ".viewport");
    ASSERT_NE (viewport, nullptr);
    EXPECT_EQ (viewport->getTitle(), "Recorded channels viewport");

    auto* content = findDescendantById (selector, prefix + ".content");
    ASSERT_NE (content, nullptr);
    EXPECT_EQ (content->getTitle(), "Recorded channels choices");

    auto* secondChannel = selector.getButtonForId (1);
    ASSERT_NE (secondChannel, nullptr);
    EXPECT_EQ (secondChannel->getComponentID(), prefix + ".channel_2");
    EXPECT_EQ (secondChannel->getTitle(), "AP2");
    EXPECT_EQ (secondChannel->getDescription(),
               "Select or deselect channel 2 (AP2).");
    auto secondHandler = secondChannel->createAccessibilityHandler();
    ASSERT_NE (secondHandler, nullptr);
    EXPECT_EQ (
        secondHandler->getRole(),
        AccessibilityRole::toggleButton);
    EXPECT_FALSE (
        secondHandler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        secondHandler->getActions().contains (AccessibilityActionType::toggle));
    ASSERT_NE (
        secondHandler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        secondHandler->getValueInterface()
            ->isReadOnly());
    EXPECT_FALSE (
        secondHandler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        secondHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Off");

    auto* selectAll = findDescendantById (selector, prefix + ".select_all");
    ASSERT_NE (selectAll, nullptr);
    EXPECT_EQ (selectAll->getTitle(), "Select all channels");
    auto selectAllHandler =
        selectAll->createAccessibilityHandler();
    ASSERT_NE (selectAllHandler, nullptr);
    EXPECT_EQ (
        selectAllHandler->getRole(),
        AccessibilityRole::button);
    EXPECT_TRUE (
        selectAllHandler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_FALSE (
        selectAllHandler->getActions().contains (
            AccessibilityActionType::toggle));

    auto* selectNone = findDescendantById (selector, prefix + ".select_none");
    ASSERT_NE (selectNone, nullptr);
    EXPECT_EQ (selectNone->getTitle(), "Select no channels");

    auto* selectRange = findDescendantById (selector, prefix + ".select_range");
    ASSERT_NE (selectRange, nullptr);
    EXPECT_EQ (selectRange->getTitle(), "Select channel range");

    auto* range = findDescendantById (selector, prefix + ".range");
    ASSERT_NE (range, nullptr);
    EXPECT_EQ (range->getTitle(), "Channel range");
    auto rangeHandler = range->createAccessibilityHandler();
    ASSERT_NE (rangeHandler, nullptr);
    ASSERT_NE (rangeHandler->getValueInterface(), nullptr);
    EXPECT_FALSE (rangeHandler->getValueInterface()->isReadOnly());
}

TEST_F (PopupChannelSelectorAccessibilityTests, WorkerBulkCommandsRunOnMessageThread)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    PopupChannelSelector selector (
        &anchor,
        &listener,
        initialStates(),
        channelNames(),
        "Recorded channels");
    const String prefix =
        "oe.processor.100.parameter.channels.popup";
    auto* selectNone =
        findDescendantById (
            selector,
            prefix + ".select_none");
    auto* selectAll =
        findDescendantById (
            selector,
            prefix + ".select_all");
    auto* selectRange =
        findDescendantById (
            selector,
            prefix + ".select_range");
    auto* range =
        dynamic_cast<TextEditor*> (
            findDescendantById (
                selector,
                prefix + ".range"));
    ASSERT_NE (selectNone, nullptr);
    ASSERT_NE (selectAll, nullptr);
    ASSERT_NE (selectRange, nullptr);
    ASSERT_NE (range, nullptr);
    auto noneHandler =
        selectNone
            ->createAccessibilityHandler();
    auto allHandler =
        selectAll
            ->createAccessibilityHandler();
    auto rangeHandler =
        selectRange
            ->createAccessibilityHandler();
    ASSERT_NE (noneHandler, nullptr);
    ASSERT_NE (allHandler, nullptr);
    ASSERT_NE (rangeHandler, nullptr);

    for (int repeat = 0;
         repeat < 2;
         ++repeat)
    {
        listener
            .changeUsedMessageThread
            .store (false);
        EXPECT_TRUE (
            invokeFromWorker (
                noneHandler->getActions(),
                AccessibilityActionType::press));
        EXPECT_EQ (
            listener.changeCount,
            repeat + 1);
        EXPECT_TRUE (
            listener
                .changeUsedMessageThread
                .load());
        EXPECT_TRUE (
            listener.selectedChannels
                .isEmpty());
    }
    for (int channel = 0;
         channel < listener.channelCount;
         ++channel)
    {
        auto handler =
            selector.getButtonForId (
                        channel)
                ->createAccessibilityHandler();
        EXPECT_FALSE (
            handler->getCurrentState()
                .isChecked());
    }

    for (int repeat = 0;
         repeat < 2;
         ++repeat)
    {
        listener
            .changeUsedMessageThread
            .store (false);
        EXPECT_TRUE (
            invokeFromWorker (
                allHandler->getActions(),
                AccessibilityActionType::press));
        EXPECT_EQ (
            listener.changeCount,
            repeat + 3);
        EXPECT_TRUE (
            listener
                .changeUsedMessageThread
                .load());
        EXPECT_EQ (
            listener.selectedChannels.size(),
            listener.channelCount);
    }

    range->setText (
        "2:2:7",
        dontSendNotification);
    for (int repeat = 0;
         repeat < 2;
         ++repeat)
    {
        listener
            .changeUsedMessageThread
            .store (false);
        EXPECT_TRUE (
            invokeFromWorker (
                rangeHandler->getActions(),
                AccessibilityActionType::press));
        EXPECT_EQ (
            listener.changeCount,
            repeat + 5);
        EXPECT_TRUE (
            listener
                .changeUsedMessageThread
                .load());
        EXPECT_EQ (
            listener.selectedChannels,
            Array<int> ({ 1, 3, 5 }));
    }
}

TEST_F (PopupChannelSelectorAccessibilityTests, DisabledAndStaleBulkCommandsAreNoOps)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    auto selector =
        std::make_unique<
            PopupChannelSelector> (
            &anchor,
            &listener,
            initialStates(),
            channelNames(),
            "Recorded channels");
    auto* selectNone =
        findDescendantById (
            *selector,
            "oe.processor.100.parameter.channels.popup.select_none");
    ASSERT_NE (selectNone, nullptr);
    auto handler =
        selectNone
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    selectNone->setEnabled (false);
    EXPECT_TRUE (
        invokeFromWorker (
            handler->getActions(),
            AccessibilityActionType::press));
    EXPECT_EQ (listener.changeCount, 0);
    EXPECT_EQ (
        listener.selectedChannels,
        Array<int> ({ 0 }));

    const auto staleActions =
        handler->getActions();
    handler.reset();
    selector.reset();
    EXPECT_TRUE (
        invokeFromWorker (
            staleActions,
            AccessibilityActionType::press));
    EXPECT_EQ (listener.changeCount, 0);
}

TEST_F (PopupChannelSelectorAccessibilityTests, ListenerCanDestroyPopupDuringWorkerBulkCommand)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    auto selector =
        std::make_unique<
            PopupChannelSelector> (
            &anchor,
            &listener,
            initialStates(),
            channelNames(),
            "Recorded channels");
    auto* selectNone =
        findDescendantById (
            *selector,
            "oe.processor.100.parameter.channels.popup.select_none");
    ASSERT_NE (selectNone, nullptr);
    auto handler =
        selectNone
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();
    handler.reset();
    listener.onChange =
        [&selector]
        {
            selector.reset();
        };

    EXPECT_TRUE (
        invokeFromWorker (
            actions,
            AccessibilityActionType::press));
    EXPECT_EQ (selector, nullptr);
    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_TRUE (
        listener
            .changeUsedMessageThread
            .load());

    listener.onChange = {};
    EXPECT_TRUE (
        invokeFromWorker (
            actions,
            AccessibilityActionType::press));
    EXPECT_EQ (listener.changeCount, 1);
}

TEST_F (PopupChannelSelectorAccessibilityTests, ListenerCanDestroyPopupDuringWorkerSelectAll)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    auto selector =
        std::make_unique<
            PopupChannelSelector> (
            &anchor,
            &listener,
            initialStates(),
            channelNames(),
            "Recorded channels");
    auto* selectAll =
        findDescendantById (
            *selector,
            "oe.processor.100.parameter.channels.popup.select_all");
    ASSERT_NE (selectAll, nullptr);
    auto handler =
        selectAll
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();
    handler.reset();
    listener.onChange =
        [&selector]
        {
            selector.reset();
        };

    EXPECT_TRUE (
        invokeFromWorker (
            actions,
            AccessibilityActionType::press));
    EXPECT_EQ (selector, nullptr);
    EXPECT_EQ (listener.changeCount, 1);
}

TEST_F (PopupChannelSelectorAccessibilityTests, WorkerToggleUpdatesSelectionOnMessageThread)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    PopupChannelSelector selector (
        &anchor,
        &listener,
        initialStates(),
        channelNames(),
        "Recorded channels");

    auto* secondChannel = selector.getButtonForId (1);
    auto secondHandler = secondChannel->createAccessibilityHandler();
    ASSERT_NE (secondHandler, nullptr);
    ASSERT_TRUE (
        invokeFromWorker (
            secondHandler->getActions(),
            AccessibilityActionType::toggle));

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_TRUE (
        listener
            .changeUsedMessageThread
            .load());
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 0, 1 }));
    EXPECT_TRUE (
        secondHandler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        secondHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "On");

    auto* firstChannel = selector.getButtonForId (0);
    auto firstHandler = firstChannel->createAccessibilityHandler();
    ASSERT_NE (firstHandler, nullptr);
    ASSERT_TRUE (
        invokeFromWorker (
            firstHandler->getActions(),
            AccessibilityActionType::toggle));

    EXPECT_EQ (listener.changeCount, 2);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 1 }));
    EXPECT_FALSE (
        firstHandler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        firstHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Off");
}

TEST_F (PopupChannelSelectorAccessibilityTests, RefreshedSelectionFeedsTheNextAccessibilityToggle)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    PopupChannelSelector selector (
        &anchor,
        &listener,
        initialStates(),
        channelNames(),
        "Recorded channels");

    listener.selectedChannels = { 2 };
    selector.updatePopup();

    EXPECT_EQ (listener.changeCount, 0);
    EXPECT_FALSE (selector.getButtonForId (0)->getToggleState());
    EXPECT_TRUE (selector.getButtonForId (2)->getToggleState());
    auto thirdHandler =
        selector.getButtonForId (2)->createAccessibilityHandler();
    ASSERT_NE (thirdHandler, nullptr);
    EXPECT_TRUE (
        thirdHandler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        thirdHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "On");

    auto fourthHandler =
        selector.getButtonForId (3)->createAccessibilityHandler();
    ASSERT_NE (fourthHandler, nullptr);
    ASSERT_TRUE (
        invokeFromWorker (
            fourthHandler->getActions(),
            AccessibilityActionType::toggle));

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 2, 3 }));
}

TEST_F (PopupChannelSelectorAccessibilityTests, AccessibilityToggleHonoursSelectionLimit)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    PopupChannelSelector selector (
        &anchor,
        &listener,
        initialStates(),
        channelNames(),
        "Recorded channels");
    selector.setMaximumSelectableChannels (1);

    auto firstHandler =
        selector.getButtonForId (0)->createAccessibilityHandler();
    auto secondHandler =
        selector.getButtonForId (1)->createAccessibilityHandler();
    ASSERT_NE (firstHandler, nullptr);
    ASSERT_NE (secondHandler, nullptr);
    ASSERT_TRUE (
        invokeFromWorker (
            secondHandler->getActions(),
            AccessibilityActionType::toggle));

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 1 }));
    EXPECT_FALSE (selector.getButtonForId (0)->getToggleState());
    EXPECT_TRUE (selector.getButtonForId (1)->getToggleState());
    EXPECT_FALSE (
        firstHandler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        firstHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "Off");
    EXPECT_TRUE (
        secondHandler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        secondHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "On");
}

TEST_F (PopupChannelSelectorAccessibilityTests, DisabledAndStaleChannelActionsAreNoOps)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    auto selector =
        std::make_unique<
            PopupChannelSelector> (
            &anchor,
            &listener,
            initialStates(),
            channelNames(),
            "Recorded channels");
    auto* secondChannel =
        selector->getButtonForId (1);
    ASSERT_NE (secondChannel, nullptr);
    auto secondHandler =
        secondChannel
            ->createAccessibilityHandler();
    ASSERT_NE (secondHandler, nullptr);

    secondChannel->setEnabled (false);
    EXPECT_TRUE (
        invokeFromWorker (
            secondHandler->getActions(),
            AccessibilityActionType::toggle));
    EXPECT_EQ (listener.changeCount, 0);
    EXPECT_EQ (
        listener.selectedChannels,
        Array<int> ({ 0 }));
    EXPECT_FALSE (
        secondHandler->getCurrentState()
            .isChecked());

    const auto staleActions =
        secondHandler->getActions();
    secondHandler.reset();
    selector.reset();
    EXPECT_TRUE (
        invokeFromWorker (
            staleActions,
            AccessibilityActionType::toggle));
    EXPECT_EQ (listener.changeCount, 0);
}

TEST_F (PopupChannelSelectorAccessibilityTests, ListenerCanDestroyPopupDuringWorkerToggle)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    auto selector =
        std::make_unique<
            PopupChannelSelector> (
            &anchor,
            &listener,
            initialStates(),
            channelNames(),
            "Recorded channels");
    auto handler =
        selector->getButtonForId (1)
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();
    handler.reset();
    listener.onChange =
        [&selector]
        {
            selector.reset();
        };

    EXPECT_TRUE (
        invokeFromWorker (
            actions,
            AccessibilityActionType::toggle));
    EXPECT_EQ (selector, nullptr);
    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_TRUE (
        listener
            .changeUsedMessageThread
            .load());

    listener.onChange = {};
    EXPECT_TRUE (
        invokeFromWorker (
            actions,
            AccessibilityActionType::toggle));
    EXPECT_EQ (listener.changeCount, 1);
}

TEST_F (PopupChannelSelectorAccessibilityTests, MouseSelectionNotifiesOnlyOnce)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    PopupChannelSelector selector (
        &anchor,
        &listener,
        initialStates(),
        channelNames(),
        "Recorded channels");

    auto* secondChannel = selector.getButtonForId (1);
    ASSERT_NE (secondChannel, nullptr);
    selector.startDragCoords = secondChannel->getBounds().getCentre();

    const auto eventTime = Time::getCurrentTime();
    MouseEvent mouseUpEvent (
        Desktop::getInstance().getMainMouseSource(),
        selector.startDragCoords.toFloat(),
        ModifierKeys(),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &selector,
        &selector,
        eventTime,
        selector.startDragCoords.toFloat(),
        eventTime,
        1,
        false);

    selector.mouseUp (mouseUpEvent);

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 0, 1 }));

    MessageManager::getInstance()->runDispatchLoopUntil (50);

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 0, 1 }));
}

TEST_F (PopupChannelSelectorAccessibilityTests, MouseSelectionNormalisesAnOversizedSelection)
{
    TextButton anchor ("Channels");
    applySemanticMetadata (
        anchor,
        "oe.processor.100.parameter.channels",
        "Recorded channels",
        "Choose channels to record.");
    TestChannelListener listener;
    listener.selectedChannels = { 0, 1, 2 };
    PopupChannelSelector selector (
        &anchor,
        &listener,
        { true, true, true, false, false,
          false, false, false, false, false },
        channelNames(),
        "Recorded channels");
    selector.setMaximumSelectableChannels (1);

    auto* fourthChannel = selector.getButtonForId (3);
    ASSERT_NE (fourthChannel, nullptr);
    selector.startDragCoords = fourthChannel->getBounds().getCentre();

    const auto eventTime = Time::getCurrentTime();
    MouseEvent mouseUpEvent (
        Desktop::getInstance().getMainMouseSource(),
        selector.startDragCoords.toFloat(),
        ModifierKeys(),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &selector,
        &selector,
        eventTime,
        selector.startDragCoords.toFloat(),
        eventTime,
        1,
        false);

    selector.mouseUp (mouseUpEvent);

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 3 }));
    EXPECT_FALSE (selector.getButtonForId (0)->getToggleState());
    EXPECT_FALSE (selector.getButtonForId (1)->getToggleState());
    EXPECT_FALSE (selector.getButtonForId (2)->getToggleState());
    EXPECT_TRUE (fourthChannel->getToggleState());
}

TEST_F (PopupChannelSelectorAccessibilityTests, FallbackIdsIncludeStablePopupMeaning)
{
    TextButton firstAnchor ("Channels");
    TextButton secondAnchor ("Channels");
    TestChannelListener firstListener;
    TestChannelListener secondListener;
    PopupChannelSelector recordedChannels (
        &firstAnchor,
        &firstListener,
        initialStates(),
        channelNames(),
        "Recorded channels");
    PopupChannelSelector detectedChannels (
        &secondAnchor,
        &secondListener,
        initialStates(),
        channelNames(),
        "Detected channels");

    EXPECT_EQ (recordedChannels.getComponentID(),
               "oe.popup.channel_selector.recorded_channels");
    EXPECT_EQ (detectedChannels.getComponentID(),
               "oe.popup.channel_selector.detected_channels");
    EXPECT_NE (recordedChannels.getComponentID(),
               detectedChannels.getComponentID());
}
