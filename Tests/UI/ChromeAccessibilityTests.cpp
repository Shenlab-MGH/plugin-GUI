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

TEST (ChromeAccessibilityTests, ExposesConsoleContextMenuMeaning)
{
    CodeDocument document;
    ConsoleEditor editor (document);
    PopupMenu menu;

    editor.addPopupMenuItems (menu, nullptr);

    PopupMenu::MenuItemIterator iterator (menu);

    ASSERT_TRUE (iterator.next());
    EXPECT_EQ (iterator.getItem().accessibilityId,
               "oe.console.context.copy");
    EXPECT_EQ (iterator.getItem().accessibilityDescription,
               "Copy the selected console text.");

    ASSERT_TRUE (iterator.next());
    EXPECT_EQ (iterator.getItem().accessibilityId,
               "oe.console.context.select_all");
    EXPECT_EQ (iterator.getItem().accessibilityDescription,
               "Select all console text.");

    ASSERT_TRUE (iterator.next());
    EXPECT_TRUE (iterator.getItem().isSeparator);

    ASSERT_TRUE (iterator.next());
    EXPECT_EQ (iterator.getItem().accessibilityId,
               "oe.console.context.reset_font_size");
    EXPECT_EQ (iterator.getItem().accessibilityDescription,
               "Reset the console font size to 14 points.");
}

TEST (ChromeAccessibilityTests, OpensConsoleContextMenuThroughAccessibility)
{
    CodeDocument document;
    ConsoleEditor editor (document);

    auto handler = editor.createAccessibilityHandler();

    ASSERT_NE (handler, nullptr);
    EXPECT_NE (handler->getTextInterface(), nullptr);
    EXPECT_TRUE (handler->getCurrentState().isExpandable());
    EXPECT_TRUE (handler->getCurrentState().isCollapsed());
    EXPECT_TRUE (
        handler->getActions().contains (AccessibilityActionType::showMenu));
}

TEST (ChromeAccessibilityTests, CollapsesAnOpenConsoleContextMenu)
{
    CodeDocument document;
    ConsoleEditor editor (document);

    auto handler = editor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    ASSERT_TRUE (
        handler->getActions().invoke (AccessibilityActionType::showMenu));
    EXPECT_TRUE (handler->getCurrentState().isExpanded());

    ASSERT_TRUE (
        handler->getActions().invoke (AccessibilityActionType::showMenu));
    EXPECT_TRUE (handler->getCurrentState().isCollapsed());
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
