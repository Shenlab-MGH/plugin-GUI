#include "../../Source/UI/GraphViewer.h"
#include "gtest/gtest.h"

namespace
{
class GraphViewerTestApplication final
    : public JUCEApplication
{
public:
    const String getApplicationName() override
    {
        return "Open Ephys Graph UIA Test";
    }

    const String getApplicationVersion() override
    {
        return "1.0.2";
    }

    void initialise (const String&) override {}
    void shutdown() override {}
};

class GraphNodeTestProcessor final
    : public GenericProcessor
{
public:
    GraphNodeTestProcessor (
        int nodeId = 42,
        bool withDetails = true)
        : GenericProcessor (
              "Graph Test Processor")
    {
        setNodeId (nodeId);

        if (withDetails)
        {
            addBooleanParameter (
                Parameter::PROCESSOR_SCOPE,
                "enabled",
                "Enabled",
                "Enable graph test processing.",
                true);
        }
    }

    void process (
        AudioBuffer<float>&) override
    {
    }
};

class GraphNodeTestEditor final
    : public GenericEditor
{
public:
    explicit GraphNodeTestEditor (
        GenericProcessor* processor)
        : GenericEditor (processor)
    {
    }
};

JUCEApplicationBase*
createGraphViewerTestApplication()
{
    return new GraphViewerTestApplication();
}

class GraphViewerAccessibilityTests
    : public ::testing::Test
{
protected:
    void SetUp() override
    {
        AccessClass::
            clearAccessClassStateForTesting();
        JUCEApplicationBase::createInstance =
            createGraphViewerTestApplication;
        application =
            std::make_unique<
                GraphViewerTestApplication>();
        MessageManager::getInstance();
        messageManagerLock =
            std::make_unique<
                MessageManagerLock>();
    }

    void TearDown() override
    {
        messageManagerLock.reset();
        AccessClass::
            clearAccessClassStateForTesting();
        application.reset();
        JUCEApplicationBase::createInstance =
            nullptr;
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<
        GraphViewerTestApplication>
        application;
    std::unique_ptr<MessageManagerLock>
        messageManagerLock;
};

Component* findAccessibleChild (
    AccessibilityHandler& parent,
    StringRef componentId)
{
    const auto children =
        parent.getChildren();
    const auto match =
        std::find_if (
            children.begin(),
            children.end(),
            [componentId] (
                const AccessibilityHandler*
                    child)
            {
                return child != nullptr
                       && child
                              ->getComponent()
                              .getComponentID()
                              == componentId;
            });

    return match != children.end()
               ? &(*match)->getComponent()
               : nullptr;
}
} // namespace

TEST_F (GraphViewerAccessibilityTests,
        ExposesGraphNavigationAndScrollbars)
{
    GraphViewer graph;
    auto* navigation =
        graph.getGraphViewport();
    ASSERT_NE (navigation, nullptr);

    graph.setBounds (0, 0, 1600, 1200);
    navigation->setBounds (
        0,
        0,
        800,
        400);
    navigation->addToDesktop (0);

    EXPECT_EQ (
        navigation->getComponentID(),
        "oe.graph.navigation");
    EXPECT_EQ (
        navigation->getTitle(),
        "Processor graph navigation");
    EXPECT_EQ (
        navigation->getDescription(),
        "Navigate the processor graph.");
    EXPECT_TRUE (
        navigation->isFocusContainer());

    auto* navigationHandler =
        navigation
            ->getAccessibilityHandler();
    ASSERT_NE (navigationHandler, nullptr);
    EXPECT_EQ (
        navigationHandler->getRole(),
        AccessibilityRole::group);

    Viewport* viewport = nullptr;
    for (auto* child :
         navigation->getChildren())
    {
        if (auto* candidate =
                dynamic_cast<Viewport*> (
                    child))
        {
            viewport = candidate;
            break;
        }
    }

    ASSERT_NE (viewport, nullptr);
    EXPECT_EQ (
        viewport->getComponentID(),
        "oe.graph.viewport");
    EXPECT_EQ (
        viewport->getTitle(),
        "Processor graph viewport");
    EXPECT_EQ (
        viewport->getDescription(),
        "Scroll through the processor graph.");
    EXPECT_TRUE (
        viewport->isFocusContainer());

    auto* viewportHandler =
        viewport->getAccessibilityHandler();
    ASSERT_NE (viewportHandler, nullptr);
    EXPECT_EQ (
        viewportHandler->getRole(),
        AccessibilityRole::group);
    ASSERT_NE (
        viewportHandler->getParent(),
        nullptr);
    EXPECT_EQ (
        &viewportHandler
             ->getParent()
             ->getComponent(),
        navigation);

    EXPECT_EQ (
        graph.getComponentID(),
        "oe.graph.content");
    EXPECT_EQ (
        graph.getTitle(),
        "Processor graph");
    EXPECT_EQ (
        graph.getDescription(),
        "Inspect and select processors and data streams.");
    EXPECT_TRUE (
        graph.isFocusContainer());

    auto* graphHandler =
        graph.getAccessibilityHandler();
    ASSERT_NE (graphHandler, nullptr);
    EXPECT_EQ (
        graphHandler->getRole(),
        AccessibilityRole::group);

    EXPECT_EQ (
        findAccessibleChild (
            *navigationHandler,
            "oe.graph.viewport"),
        viewport);
    EXPECT_EQ (
        findAccessibleChild (
            *viewportHandler,
            "oe.graph.content"),
        &graph);

    struct ExpectedScrollBar
    {
        ScrollBar* scrollBar;
        const char* id;
        const char* title;
        const char* description;
    };

    const std::array<ExpectedScrollBar, 2>
        expectedScrollBars {
            ExpectedScrollBar {
                &viewport
                     ->getHorizontalScrollBar(),
                "oe.graph.viewport.horizontal_scrollbar",
                "Processor graph horizontal scroll",
                "Scroll horizontally through the processor graph." },
            ExpectedScrollBar {
                &viewport
                     ->getVerticalScrollBar(),
                "oe.graph.viewport.vertical_scrollbar",
                "Processor graph vertical scroll",
                "Scroll vertically through the processor graph." }
        };

    for (const auto& expected :
         expectedScrollBars)
    {
        EXPECT_EQ (
            expected.scrollBar
                ->getComponentID(),
            expected.id);
        EXPECT_EQ (
            expected.scrollBar->getTitle(),
            expected.title);
        EXPECT_EQ (
            expected.scrollBar
                ->getDescription(),
            expected.description);

        auto* handler =
            expected.scrollBar
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::scrollBar);
        ASSERT_NE (
            handler->getValueInterface(),
            nullptr);
        EXPECT_FALSE (
            handler->getValueInterface()
                ->isReadOnly());
        EXPECT_EQ (
            findAccessibleChild (
                *viewportHandler,
                expected.id),
            expected.scrollBar);
    }
}

TEST_F (GraphViewerAccessibilityTests,
        ExposesGraphNodeSelectionAndDetails)
{
    GraphNodeTestProcessor processor;
    GraphNodeTestEditor editor (
        &processor);
    GraphViewer graph;
    GraphNode node (
        &editor,
        &graph);
    graph.addAndMakeVisible (&node);
    graph.setBounds (
        0,
        0,
        1600,
        1200);
    auto* navigation =
        graph.getGraphViewport();
    ASSERT_NE (navigation, nullptr);
    navigation->setBounds (
        0,
        0,
        800,
        400);
    navigation->addToDesktop (0);

    EXPECT_EQ (
        node.getComponentID(),
        "oe.graph.node.42");
    EXPECT_EQ (
        node.getTitle(),
        "Graph Test Processor node 42");
    EXPECT_EQ (
        node.getDescription(),
        "Select Graph Test Processor node 42 and show or hide its details.");
    EXPECT_TRUE (
        node.isFocusContainer());

    auto* handler =
        node.getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::button);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_FALSE (
        handler->getActions().contains (
            AccessibilityActionType::toggle));
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::showMenu));

    auto state =
        handler->getCurrentState();
    EXPECT_FALSE (state.isSelectable());
    EXPECT_FALSE (state.isSelected());
    EXPECT_TRUE (state.isExpandable());
    EXPECT_TRUE (state.isCollapsed());
    ASSERT_NE (
        handler->getParent(),
        nullptr);
    EXPECT_EQ (
        &handler
             ->getParent()
             ->getComponent(),
        &graph);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        handler
            ->getValueInterface()
            ->isReadOnly());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "not selected");

    EXPECT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        editor.getSelectionState());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "selected");

    EXPECT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));
    EXPECT_TRUE (
        node.processorInfoVisible);

    state = handler->getCurrentState();
    EXPECT_TRUE (state.isExpanded());
    EXPECT_FALSE (
        handler->getChildren().empty());

    EXPECT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::showMenu));
    EXPECT_FALSE (
        node.processorInfoVisible);

    state = handler->getCurrentState();
    EXPECT_TRUE (state.isCollapsed());
}

TEST_F (GraphViewerAccessibilityTests,
        DoesNotAdvertiseUnavailableGraphNodeDetails)
{
    GraphNodeTestProcessor processor (
        43,
        false);
    GraphNodeTestEditor editor (
        &processor);
    GraphViewer graph;
    GraphNode node (
        &editor,
        &graph);

    EXPECT_EQ (
        node.getComponentID(),
        "oe.graph.node.43");
    EXPECT_EQ (
        node.getDescription(),
        "Select Graph Test Processor node 43.");

    auto handler =
        node.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_FALSE (
        handler->getActions().contains (
            AccessibilityActionType::toggle));
    EXPECT_FALSE (
        handler->getActions().contains (
            AccessibilityActionType::showMenu));
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "not selected");

    const auto state =
        handler->getCurrentState();
    EXPECT_FALSE (state.isSelectable());
    EXPECT_FALSE (state.isExpandable());
    EXPECT_FALSE (state.isExpanded());
    EXPECT_FALSE (state.isCollapsed());
}

TEST_F (GraphViewerAccessibilityTests,
        GraphNodeSelectionRunsOnMessageThread)
{
    GraphNodeTestProcessor processor (
        44,
        false);
    GraphNodeTestEditor editor (
        &processor);
    GraphViewer graph;
    GraphNode node (
        &editor,
        &graph);
    auto handler =
        node.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    std::thread worker (
        [&handler]
        {
            handler
                ->getActions()
                .invoke (
                    AccessibilityActionType::
                        press);
        });
    worker.join();

    EXPECT_FALSE (
        editor.getSelectionState());

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 20
             && ! editor
                       .getSelectionState();
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }

    EXPECT_TRUE (
        editor.getSelectionState());
}

TEST_F (GraphViewerAccessibilityTests,
        GraphNodeRefreshKeepsItsHandlerAlive)
{
    GraphNodeTestProcessor processor (
        45,
        false);
    GraphNodeTestEditor editor (
        &processor);
    GraphViewer graph;
    GraphNode node (
        &editor,
        &graph);
    graph.addAndMakeVisible (&node);
    graph.setBounds (
        0,
        0,
        800,
        400);
    auto* navigation =
        graph.getGraphViewport();
    ASSERT_NE (navigation, nullptr);
    navigation->setBounds (
        0,
        0,
        800,
        400);
    navigation->addToDesktop (0);

    auto* cachedHandler =
        node.getAccessibilityHandler();
    ASSERT_NE (cachedHandler, nullptr);

    node.setTitle ("Stale graph node title");
    node.updateWidth();

    auto allocationProbe =
        node.createAccessibilityHandler();
    ASSERT_NE (allocationProbe, nullptr);
    EXPECT_EQ (
        node.getAccessibilityHandler(),
        cachedHandler);
    EXPECT_EQ (
        node.getTitle(),
        "Graph Test Processor node 45");
    EXPECT_TRUE (
        cachedHandler
            ->getActions()
            .invoke (
                AccessibilityActionType::
                    press));
    EXPECT_EQ (
        cachedHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "selected");
}
