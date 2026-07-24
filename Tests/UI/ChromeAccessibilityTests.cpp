#include "../../Source/Processors/MessageCenter/MessageCenterEditor.h"
#include "../../Source/UI/ConsoleViewer.h"
#include "../../Source/UI/MessageCenterButton.h"
#include "gtest/gtest.h"

TEST (ChromeAccessibilityTests, ExposesConsoleOutputAndActions)
{
    Component output;
    TextButton copy;
    TextButton clear;

    configureConsoleAccessibility (output, copy, clear);

    EXPECT_EQ (output.getComponentID(), "oe.console.output");
    EXPECT_EQ (output.getTitle(), "Console output");
    EXPECT_EQ (copy.getComponentID(), "oe.console.copy_all");
    EXPECT_EQ (copy.getTitle(), "Copy console output");
    EXPECT_EQ (clear.getComponentID(), "oe.console.clear");
    EXPECT_EQ (clear.getTitle(), "Clear console output");
}

TEST (ChromeAccessibilityTests, ExposesMessageCenterToggleAndEditorControls)
{
    MessageCenterButton toggle;
    Component incomingMessage;
    Component outgoingMessage;
    Component incomingHistory;
    Component outgoingHistory;
    TextButton send;

    configureMessageCenterEditorAccessibility (
        incomingMessage,
        outgoingMessage,
        incomingHistory,
        outgoingHistory,
        send);

    EXPECT_EQ (toggle.getComponentID(), "oe.message_center.toggle");
    EXPECT_EQ (toggle.getTitle(), "Message center");
    auto toggleHandler = toggle.createAccessibilityHandler();
    ASSERT_NE (toggleHandler, nullptr);
    EXPECT_TRUE (toggleHandler->getCurrentState().isExpandable());
    EXPECT_TRUE (toggleHandler->getCurrentState().isCollapsed());
    EXPECT_TRUE (toggleHandler->getActions().contains (AccessibilityActionType::showMenu));

    toggle.switchState();
    EXPECT_TRUE (toggleHandler->getCurrentState().isExpanded());

    EXPECT_EQ (incomingMessage.getComponentID(), "oe.message_center.incoming.current");
    EXPECT_EQ (outgoingMessage.getComponentID(), "oe.message_center.outgoing.input");
    EXPECT_EQ (incomingHistory.getComponentID(), "oe.message_center.incoming.history");
    EXPECT_EQ (outgoingHistory.getComponentID(), "oe.message_center.outgoing.history");
    EXPECT_EQ (send.getComponentID(), "oe.message_center.outgoing.send");
    EXPECT_EQ (send.getTitle(), "Send message");
}
