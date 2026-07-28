#include "../../Source/UI/EditorViewport.h"
#include "../../Source/UI/UIComponent.h"
#include "gtest/gtest.h"

namespace
{
void expectSemanticButton (Button& button,
                           const String& expectedId,
                           const String& expectedTitle,
                           const String& expectedDescription)
{
    EXPECT_EQ (button.getComponentID(), expectedId);
    EXPECT_EQ (button.getTitle(), expectedTitle);
    EXPECT_EQ (button.getDescription(), expectedDescription);
    EXPECT_TRUE (button.isAccessible());
}
} // namespace

TEST (EditorViewportAccessibilityTests, ExposesSignalChainNavigation)
{
    SignalChainTabButton chainA (0);
    expectSemanticButton (chainA,
                          "oe.signal_chain.0.select",
                          "Signal chain A",
                          "Select signal chain A.");
    EXPECT_TRUE (chainA.getClickingTogglesState());

    SignalChainScrollButton up (SignalChainScrollButton::UP);
    expectSemanticButton (up,
                          "oe.signal_chain.scroll.up",
                          "Scroll signal chains up",
                          "Show earlier signal chains.");
    EXPECT_FALSE (up.getClickingTogglesState());

    SignalChainScrollButton down (SignalChainScrollButton::DOWN);
    expectSemanticButton (down,
                          "oe.signal_chain.scroll.down",
                          "Scroll signal chains down",
                          "Show later signal chains.");
    EXPECT_FALSE (down.getClickingTogglesState());
}

TEST (EditorViewportAccessibilityTests, ExposesOneSignalChainPanelToggle)
{
    ShowHideEditorViewportButton toggle;
    expectSemanticButton (
        toggle,
        "oe.signal_chain.panel.toggle",
        "Signal chain panel",
        "Show or hide the signal chain editor.");

    ASSERT_EQ (toggle.getNumChildComponents(), 1);
    EXPECT_FALSE (toggle.getChildComponent (0)->isAccessible());
}

TEST (EditorViewportAccessibilityTests, ResolvesAppendInsertionPoint)
{
    EXPECT_EQ (resolveProcessorInsertionPoint (-1, 0), 0);
    EXPECT_EQ (resolveProcessorInsertionPoint (-1, 3), 3);
    EXPECT_EQ (resolveProcessorInsertionPoint (1, 3), 1);
    EXPECT_EQ (resolveProcessorInsertionPoint (99, 3), 3);
}

TEST (EditorViewportAccessibilityTests,
      ExposesSignalChainViewportNavigation)
{
    SignalChainTabComponent navigation;
    // EditorViewport registers itself as the viewed component and transfers
    // its lifetime to the navigation viewport.
    auto* editorViewport =
        new EditorViewport (&navigation);
    navigation.setBounds (0, 0, 800, 400);
    editorViewport->setBounds (
        0,
        0,
        1600,
        400);
    navigation.addToDesktop (0);

    EXPECT_EQ (
        navigation.getComponentID(),
        "oe.signal_chain.navigation");
    EXPECT_EQ (
        navigation.getTitle(),
        "Signal chain navigation");
    EXPECT_EQ (
        navigation.getDescription(),
        "Select a signal chain and navigate its editor viewport.");
    EXPECT_TRUE (
        navigation.isFocusContainer());

    auto* navigationHandler =
        navigation.getAccessibilityHandler();
    ASSERT_NE (navigationHandler, nullptr);
    EXPECT_EQ (
        navigationHandler->getRole(),
        AccessibilityRole::group);

    auto* viewport =
        navigation.getViewport();
    ASSERT_NE (viewport, nullptr);
    EXPECT_EQ (
        viewport->getComponentID(),
        "oe.signal_chain.viewport");
    EXPECT_EQ (
        viewport->getTitle(),
        "Signal chain editor viewport");
    EXPECT_EQ (
        viewport->getDescription(),
        "Scroll through processors in the selected signal chain.");
    EXPECT_TRUE (
        viewport->isFocusContainer());

    auto* viewportHandler =
        viewport->getAccessibilityHandler();
    ASSERT_NE (viewportHandler, nullptr);
    EXPECT_EQ (
        viewportHandler->getRole(),
        AccessibilityRole::group);
    auto* viewportParent =
        viewportHandler->getParent();
    ASSERT_NE (viewportParent, nullptr);
    EXPECT_EQ (
        &viewportParent->getComponent(),
        &navigation);

    const auto hasAccessibleChild =
        [] (AccessibilityHandler& parent,
            StringRef componentId)
    {
        const auto children =
            parent.getChildren();
        return std::any_of (
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
    };

    EXPECT_TRUE (
        hasAccessibleChild (
            *navigationHandler,
            "oe.signal_chain.viewport"));
    EXPECT_TRUE (
        hasAccessibleChild (
            *viewportHandler,
            "oe.signal_chain.viewport.horizontal_scrollbar"));
    EXPECT_FALSE (
        hasAccessibleChild (
            *viewportHandler,
            "oe.signal_chain.viewport.vertical_scrollbar"));

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
                "oe.signal_chain.viewport.horizontal_scrollbar",
                "Signal chain horizontal scroll",
                "Scroll horizontally through processors in the selected signal chain." },
            ExpectedScrollBar {
                &viewport
                     ->getVerticalScrollBar(),
                "oe.signal_chain.viewport.vertical_scrollbar",
                "Signal chain vertical scroll",
                "Scroll vertically through the selected signal chain." }
        };

    for (const auto& expected :
         expectedScrollBars)
    {
        ASSERT_NE (
            expected.scrollBar,
            nullptr);
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

        if (expected.scrollBar
            == &viewport
                    ->getHorizontalScrollBar())
        {
            EXPECT_EQ (
                expected.scrollBar
                    ->findFocusContainer(),
                viewport);
            auto* scrollBarParent =
                expected.scrollBar
                    ->getAccessibilityHandler()
                    ->getParent();
            ASSERT_NE (
                scrollBarParent,
                nullptr);
            EXPECT_EQ (
                scrollBarParent
                    ->getComponent()
                    .getComponentID(),
                viewport
                    ->getComponentID());
        }
    }
}
