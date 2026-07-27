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
