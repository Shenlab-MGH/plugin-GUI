#include "../../Source/AutoUpdater.h"
#include "gtest/gtest.h"

TEST (AutoUpdaterAccessibilityTests, ExposesTheUpdatePromptControls)
{
    Component content;
    Label title;
    Label summary;
    TextEditor releaseNotes;
    TextButton download ("Download");
    TextButton cancel ("Cancel");
    ToggleButton dontAskAgain ("Don't ask again");

    configureUpdatePromptAccessibility (content,
                                        title,
                                        summary,
                                        releaseNotes,
                                        download,
                                        cancel,
                                        dontAskAgain);

    EXPECT_EQ (content.getComponentID(), "oe.dialog.update.content");
    EXPECT_EQ (title.getComponentID(), "oe.dialog.update.title");
    EXPECT_EQ (summary.getComponentID(), "oe.dialog.update.summary");
    EXPECT_EQ (releaseNotes.getComponentID(), "oe.dialog.update.release_notes");
    EXPECT_EQ (download.getComponentID(), "oe.dialog.update.download");
    EXPECT_EQ (cancel.getComponentID(), "oe.dialog.update.cancel");
    EXPECT_EQ (dontAskAgain.getComponentID(), "oe.dialog.update.dont_ask_again");

    EXPECT_EQ (download.getTitle(), "Download update");
    EXPECT_EQ (cancel.getTitle(), "Cancel update");
    EXPECT_EQ (dontAskAgain.getTitle(), "Don't ask again");
    EXPECT_FALSE (releaseNotes.getDescription().isEmpty());
    EXPECT_FALSE (download.getHelpText().isEmpty());
    EXPECT_FALSE (cancel.getHelpText().isEmpty());
    EXPECT_FALSE (dontAskAgain.getHelpText().isEmpty());
}

TEST (AutoUpdaterAccessibilityTests, ExposesTheUpdateDialogWindow)
{
    Component window;

    configureUpdateDialogAccessibility (window);

    EXPECT_EQ (window.getComponentID(), "oe.dialog.update");
    EXPECT_EQ (window.getTitle(), "Software update");
    EXPECT_FALSE (window.getDescription().isEmpty());
    EXPECT_TRUE (window.isAccessible());
}
