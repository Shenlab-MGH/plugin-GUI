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

Component* findDescendantById (Component& parent, const String& componentId)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == componentId)
            return child;

        if (auto* descendant = findDescendantById (*child, componentId))
            return descendant;
    }

    return nullptr;
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

TEST (ControlPanelAccessibilityTests, UsesClockStatusForItsAccessibleValue)
{
    TestClock clock;
    const auto state = clock.getStatus();
    auto handler = clock.createAccessibilityHandler();
    ASSERT_NE (handler->getValueInterface(), nullptr);
    EXPECT_EQ (state.display, handler->getValueInterface()->getCurrentValueAsString());
    EXPECT_EQ (state.elapsedMilliseconds, 0);
    EXPECT_FALSE (state.running);
    EXPECT_FALSE (state.recording);
}

TEST (ControlPanelAccessibilityTests, SharesForceNewDirectoryStateWithTheButton)
{
    ControlPanel panel (nullptr, nullptr, true);
    EXPECT_FALSE (panel.getRecordingOptionsStatus().newDirectoryRequestAvailable);

    panel.setForceNewDirectory (true);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().forceNewDirectory);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().newDirectoryRequested);
    EXPECT_FALSE (panel.getRecordingOptionsStatus().newDirectoryRequestAvailable);

    panel.setNewDirectoryRequested (false);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().newDirectoryRequested);

    panel.setForceNewDirectory (false);
    EXPECT_FALSE (panel.getRecordingOptionsStatus().forceNewDirectory);

    panel.setNewDirectoryRequested (false);
    EXPECT_FALSE (panel.getRecordingOptionsStatus().newDirectoryRequested);

    panel.setRecordingOptionsExpanded (true);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().expanded);
}

TEST (ControlPanelAccessibilityTests, ExposesRecordingDirectoryEditorAndBrowser)
{
    ControlPanel panel (nullptr, nullptr, true);

    auto* directory = findDescendantById (panel, "oe.control.recording.directory");
    ASSERT_NE (directory, nullptr);
    EXPECT_NE (dynamic_cast<ComboBox*> (directory), nullptr);
    EXPECT_EQ (directory->getTitle(), "Recording directory");
    EXPECT_EQ (directory->getDescription(),
               "Choose the parent directory inherited by new Record Nodes.");
    auto directoryHandler = directory->createAccessibilityHandler();
    ASSERT_NE (directoryHandler, nullptr);
    EXPECT_EQ (directoryHandler->getRole(), AccessibilityRole::comboBox);
    ASSERT_NE (directoryHandler->getValueInterface(), nullptr);
    EXPECT_EQ (directoryHandler->getValueInterface()->getCurrentValueAsString(),
               panel.getRecordingParentDirectory().getFullPathName());

    auto* browse = findDescendantById (panel, "oe.control.recording.directory.browse");
    ASSERT_NE (browse, nullptr);
    EXPECT_NE (dynamic_cast<Button*> (browse), nullptr);
    EXPECT_EQ (browse->getTitle(), "Browse recording directory");
    EXPECT_EQ (browse->getDescription(), "Choose the parent recording directory.");
    auto browseHandler = browse->createAccessibilityHandler();
    ASSERT_NE (browseHandler, nullptr);
    EXPECT_EQ (browseHandler->getRole(), AccessibilityRole::button);
}

TEST (ControlPanelAccessibilityTests, RetainsRecordingDirectorySemanticsAfterLookAndFeelChanges)
{
    ControlPanel panel (nullptr, nullptr, true);

    auto* directory = findDescendantById (panel, "oe.control.recording.directory");
    ASSERT_NE (directory, nullptr);
    auto* filenameComponent = dynamic_cast<FilenameComponent*> (directory->getParentComponent());
    ASSERT_NE (filenameComponent, nullptr);

    filenameComponent->lookAndFeelChanged();

    EXPECT_NE (findDescendantById (panel, "oe.control.recording.directory"), nullptr);
    EXPECT_NE (findDescendantById (panel, "oe.control.recording.directory.browse"), nullptr);
}

TEST (ControlPanelAccessibilityTests, ExposesDefaultRecordingEngineSelector)
{
    ControlPanel panel (nullptr, nullptr, true);

    auto* engine = findDescendantById (panel, "oe.control.recording.engine");
    ASSERT_NE (engine, nullptr);
    EXPECT_NE (dynamic_cast<ComboBox*> (engine), nullptr);
    EXPECT_EQ (engine->getTitle(), "Recording engine");
    EXPECT_EQ (engine->getDescription(),
               "Choose the default recording engine for future Record Nodes.");
    auto handler = engine->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::comboBox);
    EXPECT_NE (handler->getValueInterface(), nullptr);
}

TEST (ControlPanelAccessibilityTests, QueuedUpdatesDoNotOutlivePanel)
{
    auto* messageManager = MessageManager::getInstance();

    {
        ControlPanel panel (nullptr, nullptr, true);
        panel.createNewRecordingDirectory();
    }

    messageManager->runDispatchLoopUntil (20);
    SUCCEED();
}
