#include "../../Source/UI/MessageWindow.h"
#include "gtest/gtest.h"

TEST (MessageWindowAccessibilityTests, ExposesMessageControls)
{
    Component content;
    Label timestamp;
    TextButton resetTimestamp;
    Label message;
    TextButton send;
    ComboBox savedMessages;
    TextButton clearSavedMessages;

    configureMessageWindowAccessibility (content,
                                         timestamp,
                                         resetTimestamp,
                                         message,
                                         send,
                                         savedMessages,
                                         clearSavedMessages);

    EXPECT_EQ (content.getComponentID(), "oe.dialog.message.content");
    EXPECT_EQ (timestamp.getComponentID(), "oe.dialog.message.timestamp");
    EXPECT_EQ (resetTimestamp.getComponentID(), "oe.dialog.message.reset_timestamp");
    EXPECT_EQ (message.getComponentID(), "oe.dialog.message.text");
    EXPECT_EQ (send.getComponentID(), "oe.dialog.message.send");
    EXPECT_EQ (savedMessages.getComponentID(), "oe.dialog.message.saved_messages");
    EXPECT_EQ (clearSavedMessages.getComponentID(), "oe.dialog.message.clear_saved_messages");

    EXPECT_EQ (message.getTitle(), "Message text");
    EXPECT_FALSE (message.getDescription().isEmpty());
    EXPECT_FALSE (send.getHelpText().isEmpty());
    EXPECT_FALSE (clearSavedMessages.getHelpText().isEmpty());
}

TEST (MessageWindowAccessibilityTests, ExposesTheDialogWindow)
{
    Component window;

    configureMessageWindowDialogAccessibility (window);

    EXPECT_EQ (window.getComponentID(), "oe.dialog.message");
    EXPECT_EQ (window.getTitle(), "Message window");
    EXPECT_TRUE (window.isAccessible());
}

TEST (MessageWindowAccessibilityTests, GivesAnEmptyEditableMessageAnAccessibleName)
{
    Component content;
    Label timestamp;
    TextButton resetTimestamp;
    Label message;
    TextButton send;
    ComboBox savedMessages;
    TextButton clearSavedMessages;
    message.setEditable (true);

    configureMessageWindowAccessibility (content,
                                         timestamp,
                                         resetTimestamp,
                                         message,
                                         send,
                                         savedMessages,
                                         clearSavedMessages);

    auto handler = message.createAccessibilityHandler();

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getTitle(), "Message text");

    message.setText ("Example payload", dontSendNotification);
    EXPECT_EQ (handler->getTitle(), "Message text");
}

TEST (MessageWindowAccessibilityTests, ExposesEditableMessageThroughAWritableValue)
{
    Component content;
    Label timestamp;
    TextButton resetTimestamp;
    Label message;
    TextButton send;
    ComboBox savedMessages;
    TextButton clearSavedMessages;
    message.setEditable (true);
    configureMessageWindowAccessibility (content,
                                         timestamp,
                                         resetTimestamp,
                                         message,
                                         send,
                                         savedMessages,
                                         clearSavedMessages);

    auto handler = message.createAccessibilityHandler();
    auto* value = handler->getValueInterface();

    ASSERT_NE (value, nullptr);
    EXPECT_FALSE (value->isReadOnly());

    value->setValueAsString ("Agent-authored message");

    EXPECT_EQ (message.getText(), "Agent-authored message");

    value->setValueAsString (String::repeatedString ("x", 500));

    EXPECT_EQ (message.getText().length(), 490);
}
