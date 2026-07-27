#include "../../Source/AccessClass.h"
#include "../../Source/Processors/Editors/PopupChannelSelector.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/UI/SemanticComponent.h"
#include "gtest/gtest.h"

namespace
{
class TestChannelListener final : public PopupChannelSelector::Listener
{
public:
    Array<int> getSelectedChannels() override { return selectedChannels; }

    void channelStateChanged (Array<int> selected) override
    {
        selectedChannels = std::move (selected);
        ++changeCount;
    }

    int getChannelCount() override { return channelCount; }

    int channelCount = 10;
    Array<int> selectedChannels { 0 };
    int changeCount = 0;
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
    EXPECT_TRUE (
        secondHandler->getActions().contains (AccessibilityActionType::press));
    EXPECT_TRUE (
        secondHandler->getActions().contains (AccessibilityActionType::toggle));

    auto* selectAll = findDescendantById (selector, prefix + ".select_all");
    ASSERT_NE (selectAll, nullptr);
    EXPECT_EQ (selectAll->getTitle(), "Select all channels");

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

TEST_F (PopupChannelSelectorAccessibilityTests, AccessibilityPressUpdatesSelection)
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
        secondHandler->getActions().invoke (AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 0, 1 }));

    auto* firstChannel = selector.getButtonForId (0);
    auto firstHandler = firstChannel->createAccessibilityHandler();
    ASSERT_NE (firstHandler, nullptr);
    ASSERT_TRUE (
        firstHandler->getActions().invoke (AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    EXPECT_EQ (listener.changeCount, 2);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 1 }));
}

TEST_F (PopupChannelSelectorAccessibilityTests, RefreshedSelectionFeedsTheNextAccessibilityPress)
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

    auto fourthHandler =
        selector.getButtonForId (3)->createAccessibilityHandler();
    ASSERT_NE (fourthHandler, nullptr);
    ASSERT_TRUE (
        fourthHandler->getActions().invoke (AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 2, 3 }));
}

TEST_F (PopupChannelSelectorAccessibilityTests, AccessibilityPressHonoursSelectionLimit)
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

    auto secondHandler =
        selector.getButtonForId (1)->createAccessibilityHandler();
    ASSERT_NE (secondHandler, nullptr);
    ASSERT_TRUE (
        secondHandler->getActions().invoke (AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    EXPECT_EQ (listener.changeCount, 1);
    EXPECT_EQ (listener.selectedChannels, Array<int> ({ 1 }));
    EXPECT_FALSE (selector.getButtonForId (0)->getToggleState());
    EXPECT_TRUE (selector.getButtonForId (1)->getToggleState());
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
