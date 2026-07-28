#include "../../Source/Processors/Merger/Merger.h"
#include "../../Source/Processors/Merger/MergerEditor.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "../../Source/Processors/Splitter/Splitter.h"
#include "../../Source/Processors/Splitter/SplitterEditor.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

namespace
{
class TestRoutingSource final
    : public GenericProcessor
{
public:
    TestRoutingSource (
        String name,
        int nodeId)
        : GenericProcessor (
            std::move (
                name))
    {
        setNodeId (
            nodeId);
    }

    void process (
        AudioBuffer<float>&)
        override
    {
    }
};

class ProcessorRoutingGraphTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        messageManagerLock =
            std::make_unique<MessageManagerLock>();
        AccessClass::clearAccessClassStateForTesting();
        processorGraph =
            std::make_unique<ProcessorGraph> (true);
    }

    void TearDown() override
    {
        PopupMenu::dismissAllActiveMenus();
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        processorGraph.reset();
        AccessClass::clearAccessClassStateForTesting();
        Parameter::parameterMap.clear();
        messageManagerLock.reset();
    }

    std::unique_ptr<MessageManagerLock> messageManagerLock;
    std::unique_ptr<ProcessorGraph> processorGraph;
};

Component* findDescendantById (
    Component& parent,
    StringRef componentId)
{
    for (auto* child :
         parent.getChildren())
    {
        if (child->getComponentID()
            == componentId)
            return child;

        if (auto* descendant =
                findDescendantById (
                    *child,
                    componentId))
            return descendant;
    }

    return nullptr;
}

Component* findDesktopComponentById (
    StringRef componentId)
{
    auto& desktop =
        Desktop::getInstance();

    for (int index = 0;
         index
         < desktop
               .getNumComponents();
         ++index)
    {
        auto* component =
            desktop
                .getComponent (
                    index);
        if (component == nullptr)
            continue;

        if (component
                ->getComponentID()
            == componentId)
            return component;

        if (auto* descendant =
                findDescendantById (
                    *component,
                    componentId))
            return descendant;
    }

    return nullptr;
}

Component* findMenuItemWithoutSemanticId (
    Component& parent)
{
    for (auto* child :
         parent.getChildren())
    {
        if (auto* handler =
                child
                    ->getAccessibilityHandler())
        {
            if (handler->getRole()
                    == AccessibilityRole::menuItem
                && child
                       ->getComponentID()
                       .isEmpty())
            {
                return child;
            }
        }

        if (auto* descendant =
                findMenuItemWithoutSemanticId (
                    *child))
            return descendant;
    }

    return nullptr;
}

Component* findDesktopMenuItemWithoutSemanticId()
{
    auto& desktop =
        Desktop::getInstance();

    for (int index = 0;
         index < desktop.getNumComponents();
         ++index)
    {
        if (auto* component =
                desktop.getComponent (index))
        {
            if (auto* result =
                    findMenuItemWithoutSemanticId (
                        *component))
                return result;
        }
    }

    return nullptr;
}

void expectRouteChoice (
    Component& editor,
    StringRef componentId,
    StringRef title,
    StringRef description,
    bool expectedChecked)
{
    auto* component =
        findDescendantById (
            editor,
            componentId);
    ASSERT_NE (
        component,
        nullptr);
    EXPECT_EQ (
        component->getTitle(),
        title);
    EXPECT_EQ (
        component
            ->getDescription(),
        description);
    EXPECT_TRUE (
        component
            ->isAccessible());

    auto* button =
        dynamic_cast<
            Button*> (
                component);
    ASSERT_NE (
        button,
        nullptr);
    auto* handler =
        button
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::
            radioButton);
    EXPECT_TRUE (
        handler
            ->getActions()
            .contains (
                AccessibilityActionType::
                    press));
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isCheckable());
    EXPECT_EQ (
        handler
            ->getCurrentState()
            .isChecked(),
        expectedChecked);
}
} // namespace

TEST (ProcessorRoutingAccessibilityTests,
      ExposesSplitterOutputPathChoices)
{
    MessageManager::
        getInstance();
    MessageManagerLock
        messageManagerLock;

    Splitter processor;
    processor.setNodeId (101);
    auto* editor =
        dynamic_cast<
            SplitterEditor*> (
                processor
                    .createEditor());
    ASSERT_NE (
        editor,
        nullptr);
    editor->setBounds (
        0,
        0,
        200,
        150);
    editor->addToDesktop (0);

    expectRouteChoice (
        *editor,
        "oe.processor.101.route.output.a",
        "Splitter output A",
        "Show splitter output path A in the signal chain.",
        true);
    expectRouteChoice (
        *editor,
        "oe.processor.101.route.output.b",
        "Splitter output B",
        "Show splitter output path B in the signal chain.",
        false);

    editor->switchDest (1);

    expectRouteChoice (
        *editor,
        "oe.processor.101.route.output.a",
        "Splitter output A",
        "Show splitter output path A in the signal chain.",
        false);
    expectRouteChoice (
        *editor,
        "oe.processor.101.route.output.b",
        "Splitter output B",
        "Show splitter output path B in the signal chain.",
        true);
    EXPECT_EQ (
        processor.getPath(),
        1);
}

TEST (ProcessorRoutingAccessibilityTests,
      ExposesMergerInputPathChoices)
{
    MessageManager::
        getInstance();
    MessageManagerLock
        messageManagerLock;

    Merger processor;
    processor.setNodeId (102);
    auto* editor =
        dynamic_cast<
            MergerEditor*> (
                processor
                    .createEditor());
    ASSERT_NE (
        editor,
        nullptr);
    editor->setBounds (
        0,
        0,
        200,
        150);
    editor->addToDesktop (0);

    expectRouteChoice (
        *editor,
        "oe.processor.102.route.input.a",
        "Merger input A",
        "Show merger input path A in the signal chain.",
        true);
    expectRouteChoice (
        *editor,
        "oe.processor.102.route.input.b",
        "Merger input B",
        "Show merger input path B in the signal chain.",
        false);

    editor->switchSource (1);

    expectRouteChoice (
        *editor,
        "oe.processor.102.route.input.a",
        "Merger input A",
        "Show merger input path A in the signal chain.",
        false);
    expectRouteChoice (
        *editor,
        "oe.processor.102.route.input.b",
        "Merger input B",
        "Show merger input path B in the signal chain.",
        true);
    EXPECT_EQ (
        processor.getPath(),
        1);
}

TEST (ProcessorRoutingAccessibilityTests,
      OpensMergerInputMenuThroughAccessibility)
{
    MessageManager::
        getInstance();
    MessageManagerLock
        messageManagerLock;
    AccessClass::
        clearAccessClassStateForTesting();

    Merger processor;
    processor.setNodeId (103);
    auto* editor =
        dynamic_cast<
            MergerEditor*> (
            processor
                .createEditor());
    ASSERT_NE (
        editor,
        nullptr);
    editor->setBounds (
        0,
        0,
        200,
        150);
    editor->addToDesktop (0);

    auto* handler =
        editor
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::
            group);
    ASSERT_TRUE (
        handler
            ->getActions()
            .contains (
                AccessibilityActionType::
                    showMenu));
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isCollapsed());
    EXPECT_TRUE (
        handler
            ->getActions()
            .invoke (
                AccessibilityActionType::
                    showMenu));

    Component* noInputA =
        nullptr;
    for (int attempt = 0;
         attempt < 30
         && noInputA
                == nullptr;
         ++attempt)
    {
        MessageManager::
            getInstance()
                ->runDispatchLoopUntil (
                    10);
        noInputA =
            findDesktopComponentById (
                "oe.processor.103.route.input.a.none");
    }

    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isExpanded());
    ASSERT_NE (
        noInputA,
        nullptr);
    EXPECT_EQ (
        noInputA
            ->getDescription(),
        "No processor is available to connect to merger input A.");
    auto* noInputHandler =
        noInputA
            ->getAccessibilityHandler();
    ASSERT_NE (
        noInputHandler,
        nullptr);
    EXPECT_EQ (
        noInputHandler
            ->getRole(),
        AccessibilityRole::
            menuItem);
    EXPECT_EQ (
        noInputHandler
            ->getTitle(),
        "No input sources available");
    EXPECT_FALSE (
        noInputHandler
            ->isEnabled());
    EXPECT_FALSE (
        noInputHandler
            ->getActions()
            .contains (
                AccessibilityActionType::
                    press));
    EXPECT_EQ (
        findDesktopMenuItemWithoutSemanticId(),
        nullptr);

    EXPECT_TRUE (
        handler
            ->getActions()
            .invoke (
                AccessibilityActionType::
                    showMenu));
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isCollapsed());

    for (int attempt = 0;
         attempt < 30
         && findDesktopComponentById (
                "oe.processor.103.route.input.a.none")
                != nullptr;
         ++attempt)
    {
        MessageManager::
            getInstance()
                ->runDispatchLoopUntil (
                    10);
    }

    EXPECT_EQ (
        findDesktopComponentById (
            "oe.processor.103.route.input.a.none"),
        nullptr);
    AccessClass::
        clearAccessClassStateForTesting();
}

TEST (ProcessorRoutingAccessibilityTests,
      PublishesExpandedStateBeforeWorkerActionReturns)
{
    MessageManager::getInstance();
    MessageManagerLock messageManagerLock;
    AccessClass::clearAccessClassStateForTesting();

    Merger processor;
    processor.setNodeId (109);
    auto* editor =
        dynamic_cast<MergerEditor*> (
            processor.createEditor());
    ASSERT_NE (editor, nullptr);
    editor->setBounds (0, 0, 200, 150);
    editor->addToDesktop (0);

    auto* handler =
        editor->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    std::atomic<bool> actionInvoked { false };
    std::atomic<bool> expandedBeforeReturn { false };

    std::thread worker (
        [handler,
         &actionInvoked,
         &expandedBeforeReturn]
        {
            actionInvoked.store (
                handler->getActions().invoke (
                    AccessibilityActionType::showMenu));
            expandedBeforeReturn.store (
                handler->getCurrentState().isExpanded());
        });
    worker.join();

    EXPECT_TRUE (actionInvoked.load());
    EXPECT_TRUE (expandedBeforeReturn.load());

    Component* noInputA = nullptr;
    for (int attempt = 0;
         attempt < 30 && noInputA == nullptr;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        noInputA =
            findDesktopComponentById (
                "oe.processor.109.route.input.a.none");
    }
    ASSERT_NE (noInputA, nullptr);

    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));
    AccessClass::clearAccessClassStateForTesting();
}

TEST (ProcessorRoutingAccessibilityTests,
      KeepsReopenedMergerInputMenuExpanded)
{
    MessageManager::getInstance();
    MessageManagerLock messageManagerLock;
    AccessClass::clearAccessClassStateForTesting();

    Merger processor;
    processor.setNodeId (113);
    auto* editor =
        dynamic_cast<MergerEditor*> (
            processor.createEditor());
    ASSERT_NE (editor, nullptr);
    editor->setBounds (0, 0, 200, 150);
    editor->addToDesktop (0);

    auto* handler =
        editor->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));

    Component::SafePointer<Component>
        firstMenuItem;
    for (int attempt = 0;
         attempt < 30 && firstMenuItem == nullptr;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        firstMenuItem =
            findDesktopComponentById (
                "oe.processor.113.route.input.a.none");
    }
    ASSERT_NE (firstMenuItem, nullptr);

    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));
    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));

    for (int attempt = 0;
         attempt < 30
             && firstMenuItem != nullptr;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
    ASSERT_EQ (firstMenuItem, nullptr);

    Component* reopenedMenuItem = nullptr;
    for (int attempt = 0;
         attempt < 30
             && reopenedMenuItem == nullptr;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        reopenedMenuItem =
            findDesktopComponentById (
                "oe.processor.113.route.input.a.none");
    }

    ASSERT_NE (reopenedMenuItem, nullptr);
    EXPECT_TRUE (
        handler->getCurrentState().isExpanded());

    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));
    AccessClass::clearAccessClassStateForTesting();
}

TEST (ProcessorRoutingAccessibilityTests,
      ConcurrentWorkerRequestsPreserveDesiredMenuState)
{
    MessageManager::getInstance();
    MessageManagerLock messageManagerLock;
    AccessClass::clearAccessClassStateForTesting();

    Merger processor;
    processor.setNodeId (114);
    auto* editor =
        dynamic_cast<MergerEditor*> (
            processor.createEditor());
    ASSERT_NE (editor, nullptr);
    editor->setBounds (0, 0, 200, 150);
    editor->addToDesktop (0);

    auto* handler =
        editor->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    std::atomic<int> ready { 0 };
    std::atomic<bool> start { false };
    std::atomic<bool> allInvoked { true };
    const auto invokeRequests =
        [handler,
         &ready,
         &start,
         &allInvoked] (
            int requestCount)
    {
        ready.fetch_add (1);
        while (! start.load())
            std::this_thread::yield();

        for (int request = 0;
             request < requestCount;
             ++request)
        {
            if (! handler->getActions().invoke (
                    AccessibilityActionType::showMenu))
            {
                allInvoked.store (false);
            }
        }
    };

    std::thread firstWorker (
        invokeRequests,
        101);
    std::thread secondWorker (
        invokeRequests,
        100);
    while (ready.load() != 2)
        std::this_thread::yield();
    start.store (true);
    firstWorker.join();
    secondWorker.join();

    EXPECT_TRUE (allInvoked.load());
    EXPECT_TRUE (
        handler->getCurrentState().isExpanded());
    Component* menuItem = nullptr;
    for (int attempt = 0;
         attempt < 60 && menuItem == nullptr;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        menuItem =
            findDesktopComponentById (
                "oe.processor.114.route.input.a.none");
    }
    ASSERT_NE (menuItem, nullptr);
    EXPECT_TRUE (
        handler->getCurrentState().isExpanded());

    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));
    AccessClass::clearAccessClassStateForTesting();
}

TEST (ProcessorRoutingAccessibilityTests,
      PublishesCurrentMergerInputSource)
{
    MessageManager::
        getInstance();
    MessageManagerLock
        messageManagerLock;
    AccessClass::
        clearAccessClassStateForTesting();

    TestRoutingSource source (
        "Source Alpha",
        104);
    Merger processor;
    processor.setNodeId (105);
    processor.sourceNodeA =
        &source;
    auto* editor =
        dynamic_cast<
            MergerEditor*> (
            processor
                .createEditor());
    ASSERT_NE (
        editor,
        nullptr);
    editor->setBounds (
        0,
        0,
        200,
        150);
    editor->addToDesktop (0);

    auto* handler =
        editor
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    ASSERT_TRUE (
        handler
            ->getActions()
            .invoke (
                AccessibilityActionType::
                    showMenu));

    Component* currentInput =
        nullptr;
    for (int attempt = 0;
         attempt < 30
         && currentInput
                == nullptr;
         ++attempt)
    {
        MessageManager::
            getInstance()
                ->runDispatchLoopUntil (
                    10);
        currentInput =
            findDesktopComponentById (
                "oe.processor.105.route.input.a.current.104");
    }

    ASSERT_NE (
        currentInput,
        nullptr);
    EXPECT_EQ (
        currentInput
            ->getDescription(),
        "Source Alpha node 104 is connected to merger input A.");
    auto* currentInputHandler =
        currentInput
            ->getAccessibilityHandler();
    ASSERT_NE (
        currentInputHandler,
        nullptr);
    EXPECT_EQ (
        currentInputHandler
            ->getTitle(),
        "Source Alpha (104)");
    EXPECT_FALSE (
        currentInputHandler
            ->isEnabled());
    EXPECT_FALSE (
        currentInputHandler
            ->getActions()
            .contains (
                AccessibilityActionType::
                    press));

    EXPECT_TRUE (
        handler
            ->getActions()
            .invoke (
                AccessibilityActionType::
                    showMenu));
    AccessClass::
        clearAccessClassStateForTesting();
}

TEST_F (ProcessorRoutingGraphTests,
        ConnectsMergerInputSourceThroughAccessibility)
{
    Plugin::Description sourceDescription;
    sourceDescription.name = "File Reader";
    sourceDescription.type = Plugin::BUILT_IN;
    sourceDescription.index = 2;
    sourceDescription.processorType =
        Plugin::Processor::SOURCE;
    sourceDescription.nodeId = 106;
    auto secondSourceDescription =
        sourceDescription;
    secondSourceDescription.nodeId = 108;

    Plugin::Description mergerDescription;
    mergerDescription.name = "Merger";
    mergerDescription.type = Plugin::BUILT_IN;
    mergerDescription.index = 0;
    mergerDescription.processorType =
        Plugin::Processor::UTILITY;
    mergerDescription.nodeId = 107;

    auto* source =
        processorGraph->createProcessor (
            sourceDescription,
            nullptr,
            nullptr,
            true);
    auto* secondSource =
        processorGraph->createProcessor (
            secondSourceDescription,
            nullptr,
            nullptr,
            true);
    auto* merger =
        dynamic_cast<Merger*> (
            processorGraph->createProcessor (
                mergerDescription,
                nullptr,
                nullptr,
                true));
    ASSERT_NE (source, nullptr);
    ASSERT_NE (secondSource, nullptr);
    ASSERT_NE (merger, nullptr);

    auto* editor =
        dynamic_cast<MergerEditor*> (
            merger->createEditor());
    ASSERT_NE (editor, nullptr);
    editor->setBounds (0, 0, 200, 150);
    editor->addToDesktop (0);

    auto* handler =
        editor->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));

    Component* sourceItem = nullptr;
    for (int attempt = 0;
         attempt < 30 && sourceItem == nullptr;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        sourceItem =
            findDesktopComponentById (
                "oe.processor.107.route.input.a.source.106");
    }

    ASSERT_NE (sourceItem, nullptr);
    auto* sourceHandler =
        sourceItem->getAccessibilityHandler();
    ASSERT_NE (sourceHandler, nullptr);
    EXPECT_EQ (
        sourceHandler->getTitle(),
        "File Reader (106)");
    EXPECT_EQ (
        sourceItem->getDescription(),
        "Connect File Reader node 106 to merger input A.");
    ASSERT_TRUE (
        sourceHandler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        sourceHandler->getActions().invoke (
            AccessibilityActionType::press));

    for (int attempt = 0;
         attempt < 30
         && merger->getSourceNode (0) != source;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    EXPECT_EQ (merger->getSourceNode (0), source);
    EXPECT_EQ (source->getDestNode(), merger);
    EXPECT_TRUE (
        handler->getCurrentState().isCollapsed());

    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));

    Component* secondSourceItem = nullptr;
    for (int attempt = 0;
         attempt < 30
         && secondSourceItem == nullptr;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        secondSourceItem =
            findDesktopComponentById (
                "oe.processor.107.route.input.b.source.108");
    }

    ASSERT_NE (secondSourceItem, nullptr);
    auto* secondSourceHandler =
        secondSourceItem->getAccessibilityHandler();
    ASSERT_NE (secondSourceHandler, nullptr);
    ASSERT_TRUE (
        secondSourceHandler->getActions().invoke (
            AccessibilityActionType::press));

    for (int attempt = 0;
         attempt < 30
         && merger->getSourceNode (1)
                != secondSource;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    EXPECT_EQ (merger->getSourceNode (0), source);
    EXPECT_EQ (merger->getSourceNode (1), secondSource);
    EXPECT_EQ (source->getDestNode(), merger);
    EXPECT_EQ (secondSource->getDestNode(), merger);
    EXPECT_TRUE (
        handler->getCurrentState().isCollapsed());
}

TEST_F (ProcessorRoutingGraphTests,
        RejectsStaleMergerInputSelection)
{
    Plugin::Description firstSourceDescription;
    firstSourceDescription.name = "File Reader";
    firstSourceDescription.type = Plugin::BUILT_IN;
    firstSourceDescription.index = 2;
    firstSourceDescription.processorType =
        Plugin::Processor::SOURCE;
    firstSourceDescription.nodeId = 110;
    auto secondSourceDescription =
        firstSourceDescription;
    secondSourceDescription.nodeId = 111;

    Plugin::Description mergerDescription;
    mergerDescription.name = "Merger";
    mergerDescription.type = Plugin::BUILT_IN;
    mergerDescription.index = 0;
    mergerDescription.processorType =
        Plugin::Processor::UTILITY;
    mergerDescription.nodeId = 112;

    auto* firstSource =
        processorGraph->createProcessor (
            firstSourceDescription,
            nullptr,
            nullptr,
            true);
    auto* secondSource =
        processorGraph->createProcessor (
            secondSourceDescription,
            nullptr,
            nullptr,
            true);
    auto* merger =
        dynamic_cast<Merger*> (
            processorGraph->createProcessor (
                mergerDescription,
                nullptr,
                nullptr,
                true));
    ASSERT_NE (firstSource, nullptr);
    ASSERT_NE (secondSource, nullptr);
    ASSERT_NE (merger, nullptr);

    auto* editor =
        dynamic_cast<MergerEditor*> (
            merger->createEditor());
    ASSERT_NE (editor, nullptr);
    editor->setBounds (0, 0, 200, 150);
    editor->addToDesktop (0);

    auto* handler =
        editor->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));

    Component* staleFirstSourceItem = nullptr;
    for (int attempt = 0;
         attempt < 30
         && staleFirstSourceItem == nullptr;
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        staleFirstSourceItem =
            findDesktopComponentById (
                "oe.processor.112.route.input.a.source.110");
    }
    ASSERT_NE (staleFirstSourceItem, nullptr);

    processorGraph->connectMergerSource (
        merger,
        secondSource,
        0);
    processorGraph->updateSettings (merger);
    ASSERT_EQ (
        merger->getSourceNode (0),
        secondSource);

    auto* staleHandler =
        staleFirstSourceItem
            ->getAccessibilityHandler();
    ASSERT_NE (staleHandler, nullptr);
    ASSERT_TRUE (
        staleHandler->getActions().invoke (
            AccessibilityActionType::press));
    for (int attempt = 0;
         attempt < 30
         && handler->getCurrentState().isExpanded();
         ++attempt)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    EXPECT_EQ (
        merger->getSourceNode (0),
        secondSource);
    EXPECT_EQ (
        firstSource->getDestNode(),
        nullptr);
    EXPECT_EQ (
        secondSource->getDestNode(),
        merger);
}
