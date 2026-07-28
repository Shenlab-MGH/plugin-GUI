#include "../../Source/UI/GraphViewer.h"
#include "gtest/gtest.h"

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

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

    void addTestDataStream (
        const String& name =
            "Probe Stream")
    {
        auto* stream =
            new DataStream (
                DataStream::Settings {
                    name,
                    "Test probe data.",
                    "probe.stream",
                    30000.0f,
                    true });
        stream->addProcessor (this);
        stream->addParameter (
            new BooleanParameter (
                stream,
                Parameter::STREAM_SCOPE,
                "enabled",
                "Enabled",
                "Enable this probe stream.",
                true));
        dataStreams.add (stream);
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
        ASSERT_TRUE (
            static_cast<
                JUCEApplicationBase*> (
                    application.get())
                ->initialiseApp());
        messageManagerLock =
            std::make_unique<
                MessageManagerLock>();
    }

    void TearDown() override
    {
        messageManagerLock.reset();
        if (application != nullptr)
            static_cast<
                JUCEApplicationBase*> (
                    application.get())
                ->shutdownApp();
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

AccessibilityHandler*
findAccessibleDescendant (
    AccessibilityHandler& parent,
    StringRef componentId)
{
    for (auto* child :
         parent.getChildren())
    {
        if (child == nullptr)
            continue;

        if (child->getComponent()
                .getComponentID()
            == componentId)
            return child;

        if (auto* match =
                findAccessibleDescendant (
                    *child,
                    componentId))
            return match;
    }

    return nullptr;
}

Component* findComponentDescendant (
    Component& parent,
    StringRef componentId)
{
    if (parent.getComponentID()
        == componentId)
        return &parent;

    for (auto* child :
         parent.getChildren())
        if (auto* match =
                findComponentDescendant (
                    *child,
                    componentId))
            return match;

    return nullptr;
}

void collectDataStreamDetailIds (
    Component& parent,
    StringArray& ids)
{
    if (dynamic_cast<DataStreamButton*> (
            &parent)
        != nullptr
        && parent.getComponentID()
               .endsWith (".details"))
    {
        ids.add (
            parent.getComponentID());
    }

    for (auto* child :
         parent.getChildren())
        collectDataStreamDetailIds (
            *child,
            ids);
}

#if JUCE_WINDOWS
class StructureChangedRecorder final
    : public
      IUIAutomationEventHandler
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (
        REFIID interfaceId,
        void** object) override
    {
        if (object == nullptr)
            return E_POINTER;

        if (interfaceId
                == __uuidof (IUnknown)
            || interfaceId
                   == __uuidof (
                       IUIAutomationEventHandler))
        {
            *object = static_cast<
                IUIAutomationEventHandler*> (
                this);
            AddRef();
            return S_OK;
        }

        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
        override
    {
        return ++referenceCount;
    }

    ULONG STDMETHODCALLTYPE Release()
        override
    {
        const auto count =
            --referenceCount;

        if (count == 0)
            delete this;

        return count;
    }

    HRESULT STDMETHODCALLTYPE
    HandleAutomationEvent (
        IUIAutomationElement*,
        EVENTID eventId) override
    {
        if (eventId
            == UIA_StructureChangedEventId)
            ++notificationCount;

        return S_OK;
    }

    int getNotificationCount() const
    {
        return notificationCount.load();
    }

private:
    std::atomic<ULONG>
        referenceCount { 1 };
    std::atomic<int>
        notificationCount { 0 };
};
#endif
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

TEST_F (GraphViewerAccessibilityTests,
        ExposesDataStreamDetailsAndParameters)
{
    GraphNodeTestProcessor processor (
        46,
        false);
    GraphNodeTestEditor editor (
        &processor);
    processor.addTestDataStream();
    GraphViewer graph;
    GraphNode node (
        &editor,
        &graph);
    graph.addAndMakeVisible (&node);
    node.updateBoundaries();
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
    navigation->setVisible (true);
    navigation->addToDesktop (0);
    navigation->setAlwaysOnTop (
        true);
    navigation->toFront (
        false);
    ASSERT_TRUE (
        navigation->isShowing());
    ASSERT_TRUE (graph.isShowing());
    ASSERT_TRUE (node.isShowing());

    auto* nodeHandler =
        node.getAccessibilityHandler();
    ASSERT_NE (nodeHandler, nullptr);

    const String streamKey =
        "46|Probe Stream";
    const String detailsId =
        "oe.graph.node.46.stream.46_probe_stream_34367c50726f62652053747265616d.details";
    auto* details =
        findAccessibleDescendant (
            *nodeHandler,
            detailsId);
    ASSERT_NE (details, nullptr);
    EXPECT_EQ (
        details->getComponent().getTitle(),
        "Probe Stream details");
    EXPECT_EQ (
        details->getComponent()
            .getDescription(),
        "Show or hide details for the Probe Stream data stream.");
    EXPECT_EQ (
        details->getRole(),
        AccessibilityRole::button);
    EXPECT_TRUE (
        details->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        details->getActions().contains (
            AccessibilityActionType::showMenu));
    EXPECT_TRUE (
        details->getCurrentState()
            .isExpandable());
    EXPECT_TRUE (
        details->getCurrentState()
            .isCollapsed());

    EXPECT_TRUE (
        details->getActions().invoke (
            AccessibilityActionType::showMenu));
    EXPECT_TRUE (
        node.streamInfoVisible[streamKey]);
    EXPECT_TRUE (
        details->getCurrentState()
            .isExpanded());

    const String parametersId =
        "oe.graph.node.46.stream.46_probe_stream_34367c50726f62652053747265616d.parameters";
    auto* parameters =
        findAccessibleDescendant (
            *nodeHandler,
            parametersId);
    ASSERT_NE (parameters, nullptr);
    EXPECT_EQ (
        parameters->getComponent()
            .getTitle(),
        "Probe Stream parameters");
    EXPECT_EQ (
        parameters->getComponent()
            .getDescription(),
        "Show or hide parameters for the Probe Stream data stream.");
    EXPECT_TRUE (
        parameters->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        parameters->getActions().contains (
            AccessibilityActionType::showMenu));
    EXPECT_TRUE (
        parameters->getCurrentState()
            .isCollapsed());

    EXPECT_TRUE (
        parameters->getActions().invoke (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        node.streamParamsVisible[streamKey]);
    EXPECT_TRUE (
        parameters->getCurrentState()
            .isExpanded());
}

TEST_F (GraphViewerAccessibilityTests,
        DataStreamActionsRunOnMessageThread)
{
    GraphNodeTestProcessor processor (
        47,
        false);
    GraphNodeTestEditor editor (
        &processor);
    processor.addTestDataStream();
    GraphViewer graph;
    GraphNode node (
        &editor,
        &graph);
    graph.addAndMakeVisible (&node);
    node.updateBoundaries();

    auto* details =
        dynamic_cast<DataStreamButton*> (
            findComponentDescendant (
                node,
                "oe.graph.node.47.stream.47_probe_stream_34377c50726f62652053747265616d.details"));
    ASSERT_NE (details, nullptr);
    auto handler =
        details
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_FALSE (
        details->getToggleState());

    std::thread worker (
        [handler =
             handler.get()]
        {
            handler->getActions()
                .invoke (
                    AccessibilityActionType::
                        showMenu);
        });
    worker.join();

    EXPECT_FALSE (
        details->getToggleState());
    EXPECT_FALSE (
        node.streamInfoVisible[
            "47|Probe Stream"]);

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 20
             && ! details
                       ->getToggleState();
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }

    EXPECT_TRUE (
        details->getToggleState());
    EXPECT_TRUE (
        node.streamInfoVisible[
            "47|Probe Stream"]);
}

TEST_F (GraphViewerAccessibilityTests,
        DataStreamIdsRemainUniqueAfterSanitising)
{
    GraphNodeTestProcessor processor (
        48,
        false);
    GraphNodeTestEditor editor (
        &processor);
    processor.addTestDataStream (
        "Probe-A");
    processor.addTestDataStream (
        "Probe A");
    GraphViewer graph;
    GraphNode node (
        &editor,
        &graph);

    StringArray detailsIds;
    collectDataStreamDetailIds (
        node,
        detailsIds);

    ASSERT_EQ (
        detailsIds.size(),
        2);
    EXPECT_NE (
        detailsIds[0],
        detailsIds[1]);
    EXPECT_TRUE (
        isValidSemanticId (
            detailsIds[0]));
    EXPECT_TRUE (
        isValidSemanticId (
            detailsIds[1]));
}

#if JUCE_WINDOWS
TEST_F (GraphViewerAccessibilityTests,
        PublishesDataStreamStructureChangesToWindowsUia)
{
    GraphNodeTestProcessor processor (
        49,
        false);
    GraphNodeTestEditor editor (
        &processor);
    processor.addTestDataStream();
    GraphViewer graph;
    GraphNode node (
        &editor,
        &graph);
    graph.addAndMakeVisible (&node);
    node.updateBoundaries();
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
    navigation->setVisible (true);
    navigation->addToDesktop (0);
    ASSERT_TRUE (
        navigation->isShowing());
    ASSERT_NE (
        navigation
            ->getAccessibilityHandler(),
        nullptr);

    Microsoft::WRL::ComPtr<
        IUIAutomation>
        automation;
    ASSERT_TRUE (
        SUCCEEDED (
            CoCreateInstance (
                CLSID_CUIAutomation,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS (
                    &automation))));

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        rootElement;
    ASSERT_TRUE (
        SUCCEEDED (
            automation
                ->ElementFromHandle (
                    static_cast<HWND> (
                        navigation
                            ->getWindowHandle()),
                    &rootElement)));

    VARIANT automationId;
    VariantInit (
        &automationId);
    automationId.vt = VT_BSTR;
    automationId.bstrVal =
        SysAllocString (
            L"oe.graph.node.49");
    ASSERT_NE (
        automationId.bstrVal,
        nullptr);

    Microsoft::WRL::ComPtr<
        IUIAutomationCondition>
        nodeCondition;
    const auto conditionResult =
        automation
            ->CreatePropertyCondition (
                UIA_AutomationIdPropertyId,
                automationId,
                &nodeCondition);
    VariantClear (
        &automationId);
    ASSERT_TRUE (
        SUCCEEDED (
            conditionResult));

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        nodeElement;
    ASSERT_TRUE (
        SUCCEEDED (
            rootElement->FindFirst (
                TreeScope_Subtree,
                nodeCondition.Get(),
                &nodeElement)));
    if (nodeElement == nullptr)
    {
        const auto nodePoint =
            node.localPointToGlobal (
                Point<int> (
                    node.getWidth() / 2,
                    10));
        POINT screenPoint {
            nodePoint.x,
            nodePoint.y
        };
        ASSERT_TRUE (
            SUCCEEDED (
                automation
                    ->ElementFromPoint (
                        screenPoint,
                        &nodeElement)));
    }
    ASSERT_NE (
        nodeElement.Get(),
        nullptr);

    BSTR resolvedId = nullptr;
    ASSERT_TRUE (
        SUCCEEDED (
            nodeElement
                ->get_CurrentAutomationId (
                    &resolvedId)));
    ASSERT_NE (
        resolvedId,
        nullptr);
    EXPECT_EQ (
        std::wstring (
            resolvedId),
        std::wstring (
            L"oe.graph.node.49"));
    SysFreeString (
        resolvedId);

    auto* details =
        dynamic_cast<DataStreamButton*> (
            findComponentDescendant (
                node,
                "oe.graph.node.49.stream.49_probe_stream_34397c50726f62652053747265616d.details"));
    auto* parameters =
        dynamic_cast<DataStreamButton*> (
            findComponentDescendant (
                node,
                "oe.graph.node.49.stream.49_probe_stream_34397c50726f62652053747265616d.parameters"));
    ASSERT_NE (details, nullptr);
    ASSERT_NE (parameters, nullptr);
    auto detailsHandler =
        details
            ->createAccessibilityHandler();
    auto parametersHandler =
        parameters
            ->createAccessibilityHandler();
    ASSERT_NE (
        detailsHandler,
        nullptr);
    ASSERT_NE (
        parametersHandler,
        nullptr);

    auto* recorder =
        new StructureChangedRecorder();
    ASSERT_TRUE (
        SUCCEEDED (
            automation
                ->AddAutomationEventHandler (
                    UIA_StructureChangedEventId,
                    nodeElement.Get(),
                    TreeScope_Element,
                    nullptr,
                    recorder)));

    EXPECT_TRUE (
        detailsHandler
            ->getActions().invoke (
                AccessibilityActionType::
                    showMenu));
    EXPECT_TRUE (
        parametersHandler
            ->getActions().invoke (
                AccessibilityActionType::
                    showMenu));

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 20
             && recorder
                        ->getNotificationCount()
                    < 2;
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }

    EXPECT_TRUE (
        SUCCEEDED (
            automation
                ->RemoveAutomationEventHandler (
                    UIA_StructureChangedEventId,
                    nodeElement.Get(),
                    recorder)));
    EXPECT_GE (
        recorder
            ->getNotificationCount(),
        2);
    recorder->Release();
}
#endif
