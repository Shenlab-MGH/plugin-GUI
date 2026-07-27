#include "../../Source/UI/FilenameConfigWindow.h"
#include "gtest/gtest.h"

namespace
{
void expectFieldSemantics (int type,
                           int state,
                           StringRef segment,
                           StringRef name,
                           StringRef stateName)
{
    Component row;
    Label typeLabel;
    TextButton mode;
    Label value;

    configureFilenameFieldAccessibility (
        row, typeLabel, mode, value, type, state);

    const auto prefix = "oe.popup.recording_filename." + String (segment);

    EXPECT_EQ (row.getComponentID(), prefix);
    EXPECT_EQ (typeLabel.getComponentID(), prefix + ".type");
    EXPECT_EQ (mode.getComponentID(), prefix + ".mode");
    EXPECT_EQ (value.getComponentID(), prefix + ".value");
    EXPECT_EQ (mode.getTitle(),
               String (name) + " mode: " + String (stateName));
    EXPECT_EQ (value.getTitle(), String (name) + " value");
    EXPECT_FALSE (mode.getDescription().isEmpty());
    EXPECT_FALSE (value.getDescription().isEmpty());
}
} // namespace

TEST (FilenameConfigAccessibilityTests, MapsEveryFilenameFieldTypeAndState)
{
    expectFieldSemantics (FilenameFieldComponent::PREPEND,
                          FilenameFieldComponent::NONE,
                          "prepend",
                          "Prepend",
                          "None");
    expectFieldSemantics (FilenameFieldComponent::MAIN,
                          FilenameFieldComponent::AUTO,
                          "main",
                          "Main",
                          "Auto");
    expectFieldSemantics (FilenameFieldComponent::APPEND,
                          FilenameFieldComponent::CUSTOM,
                          "append",
                          "Append",
                          "Custom");
}

TEST (FilenameConfigAccessibilityTests, RefreshesThePublishedModeState)
{
    Component row;
    Label typeLabel;
    TextButton mode;
    Label value;

    configureFilenameFieldAccessibility (
        row,
        typeLabel,
        mode,
        value,
        FilenameFieldComponent::PREPEND,
        FilenameFieldComponent::NONE);
    EXPECT_EQ (mode.getTitle(), "Prepend mode: None");

    configureFilenameFieldAccessibility (
        row,
        typeLabel,
        mode,
        value,
        FilenameFieldComponent::PREPEND,
        FilenameFieldComponent::AUTO);

    EXPECT_EQ (mode.getComponentID(),
               "oe.popup.recording_filename.prepend.mode");
    EXPECT_EQ (mode.getTitle(), "Prepend mode: Auto");
}

TEST (FilenameConfigAccessibilityTests, ExposesContentAndCalloutContainers)
{
    Component content;
    Component callout;

    configureFilenameConfigAccessibility (content);
    configureFilenameCalloutAccessibility (callout);

    EXPECT_EQ (content.getComponentID(),
               "oe.popup.recording_filename.content");
    EXPECT_EQ (content.getTitle(), "Recording filename fields");
    EXPECT_EQ (callout.getComponentID(), "oe.popup.recording_filename");
    EXPECT_EQ (callout.getTitle(), "Recording filename");
    EXPECT_TRUE (content.isAccessible());
    EXPECT_TRUE (callout.isAccessible());
}

TEST (FilenameConfigAccessibilityTests, GivesAnEmptyValueItsSemanticName)
{
    Component row;
    Label typeLabel;
    TextButton mode;
    Label value;

    configureFilenameFieldAccessibility (
        row,
        typeLabel,
        mode,
        value,
        FilenameFieldComponent::PREPEND,
        FilenameFieldComponent::NONE);

    auto handler = value.createAccessibilityHandler();

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getTitle(), "Prepend value");
}

TEST (FilenameConfigAccessibilityTests, AllowsCalloutsToBeCollapsedAccessibly)
{
    Component parent;
    Component content;
    parent.setSize (500, 400);
    content.setSize (360, 100);
    CallOutBox callout (content, { 10, 10, 20, 20 }, &parent);

    auto handler = callout.createAccessibilityHandler();

    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::dialogWindow);
    EXPECT_TRUE (handler->getCurrentState().isExpandable());
    EXPECT_TRUE (handler->getCurrentState().isExpanded());
    EXPECT_TRUE (
        handler->getActions().contains (AccessibilityActionType::showMenu));
}
