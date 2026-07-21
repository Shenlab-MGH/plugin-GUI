#include "../../Source/UI/ControlPanel.h"
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

TEST (ControlPanelAccessibilityTests, ExposesGlobalActionButtons)
{
    PlayButton acquisition;
    expectSemanticButton (acquisition,
                          "oe.control.acquisition",
                          "Acquisition",
                          "Start or stop data acquisition.");

    RecordButton recording;
    expectSemanticButton (recording,
                          "oe.control.recording",
                          "Recording",
                          "Start or stop writing data to disk.");

    FilenameEditorButton filename;
    expectSemanticButton (filename,
                          "oe.control.recording.filename",
                          "Recording filename",
                          "Edit the recording filename.");

    NewDirectoryButton newDirectory;
    expectSemanticButton (newDirectory,
                          "oe.control.recording.new_directory",
                          "New recording directory",
                          "Start a new data directory for the next recording.");

    ForceNewDirectoryButton forceNewDirectory;
    expectSemanticButton (forceNewDirectory,
                          "oe.control.recording.force_new_directory",
                          "Force new recording directories",
                          "Force a new data directory for each recording.");
}
