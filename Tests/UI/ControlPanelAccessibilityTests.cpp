#include "../../Source/UI/ControlPanel.h"
#include "gtest/gtest.h"

namespace
{
class TestClock : public Clock
{
public:
    using Clock::createAccessibilityHandler;
};

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

    RecordingOptionsButton recordingOptions;
    expectSemanticButton (recordingOptions,
                          "oe.control.recording.options",
                          "Recording options",
                          "Show or hide recording options.");
    EXPECT_TRUE (recordingOptions.getClickingTogglesState());
}

TEST (ControlPanelAccessibilityTests, ExposesHealthMetersAsReadOnlyRanges)
{
    CPUMeter cpu;
    cpu.updateCPU (0.42f);

    EXPECT_EQ (cpu.getComponentID(), "oe.status.cpu_usage");
    EXPECT_EQ (cpu.getTitle(), "CPU usage");
    auto cpuHandler = cpu.createAccessibilityHandler();
    ASSERT_NE (cpuHandler, nullptr);
    EXPECT_EQ (cpuHandler->getRole(), AccessibilityRole::progressBar);
    auto* cpuValue = cpuHandler->getValueInterface();
    ASSERT_NE (cpuValue, nullptr);
    EXPECT_TRUE (cpuValue->isReadOnly());
    EXPECT_NEAR (cpuValue->getCurrentValue(), 0.42, 0.001);
    EXPECT_TRUE (cpuValue->getRange().isValid());
    EXPECT_DOUBLE_EQ (cpuValue->getRange().getMinimumValue(), 0.0);
    EXPECT_DOUBLE_EQ (cpuValue->getRange().getMaximumValue(), 1.0);

    DiskSpaceMeter disk;
    disk.updateDiskSpace (0.65f);

    EXPECT_EQ (disk.getComponentID(), "oe.status.disk_usage");
    EXPECT_EQ (disk.getTitle(), "Disk usage");
    EXPECT_EQ (disk.getDescription(), "Fraction of recording-volume space currently used.");
    auto diskHandler = disk.createAccessibilityHandler();
    ASSERT_NE (diskHandler, nullptr);
    EXPECT_EQ (diskHandler->getRole(), AccessibilityRole::progressBar);
    auto* diskValue = diskHandler->getValueInterface();
    ASSERT_NE (diskValue, nullptr);
    EXPECT_TRUE (diskValue->isReadOnly());
    EXPECT_NEAR (diskValue->getCurrentValue(), 0.65, 0.001);
    EXPECT_TRUE (diskValue->getRange().isValid());
    EXPECT_DOUBLE_EQ (diskValue->getRange().getMinimumValue(), 0.0);
    EXPECT_DOUBLE_EQ (diskValue->getRange().getMaximumValue(), 1.0);
}

TEST (ControlPanelAccessibilityTests, ExposesClockAsReadOnlyFormattedText)
{
    TestClock clock;

    EXPECT_EQ (clock.getComponentID(), "oe.status.elapsed_time");
    EXPECT_EQ (clock.getTitle(), "Elapsed time");

    auto handler = clock.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::staticText);

    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    EXPECT_TRUE (value->isReadOnly());
    EXPECT_EQ (value->getCurrentValueAsString(), "0 min 0 s");

    clock.setMode (Clock::HHMMSS);
    EXPECT_EQ (value->getCurrentValueAsString(), "00:00:00");
}
