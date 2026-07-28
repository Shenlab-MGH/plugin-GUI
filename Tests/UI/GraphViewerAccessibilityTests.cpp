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
