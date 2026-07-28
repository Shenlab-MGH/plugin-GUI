#include "../../Source/Processors/FileReader/FileReader.h"
#include "../../Source/Processors/FileReader/ScrubberInterface.h"
#include "../../Source/Processors/ProcessorGraph/ProcessorGraph.h"
#include "gtest/gtest.h"

namespace
{
class FileReaderAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        messageManagerLock =
            std::make_unique<MessageManagerLock>();
        AccessClass::clearAccessClassStateForTesting();
        processorGraph =
            std::make_unique<ProcessorGraph> (true);
    }

    void TearDown() override
    {
        processorGraph.reset();
        AccessClass::clearAccessClassStateForTesting();
        Parameter::parameterMap.clear();
        messageManagerLock.reset();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<MessageManagerLock>
        messageManagerLock;
    std::unique_ptr<ProcessorGraph>
        processorGraph;
};
} // namespace

TEST_F (FileReaderAccessibilityTests,
        ExposesEveryScrubberTimeReadout)
{
    FileReader reader;
    reader.setNodeId (100);
    ScrubberInterface scrubber (&reader);

    EXPECT_EQ (
        scrubber.getComponentID(),
        "oe.processor.100.scrubber.panel");
    EXPECT_EQ (
        scrubber.getTitle(),
        "File reader scrubber");
    auto scrubberHandler =
        scrubber.createAccessibilityHandler();
    ASSERT_NE (scrubberHandler, nullptr);
    EXPECT_EQ (
        scrubberHandler->getRole(),
        AccessibilityRole::group);

    struct ExpectedLabel
    {
        Label* label;
        const char* id;
        const char* title;
        const char* description;
        const char* value;
    };

    const std::array<ExpectedLabel, 8>
        expectedLabels {
            ExpectedLabel {
                scrubber.zoomStartTimeLabel.get(),
                "oe.processor.100.scrubber.zoom_start",
                "Zoom window start",
                "Start time of the zoom timeline.",
                "00:00:01.000" },
            ExpectedLabel {
                scrubber.zoomMiddleTimeLabel.get(),
                "oe.processor.100.scrubber.zoom_playhead",
                "Zoom playhead position",
                "Current playhead position in the zoom timeline.",
                "00:00:02.000" },
            ExpectedLabel {
                scrubber.zoomEndTimeLabel.get(),
                "oe.processor.100.scrubber.zoom_end",
                "Zoom window end",
                "End time of the zoom timeline.",
                "00:00:03.000" },
            ExpectedLabel {
                scrubber.fullStartTimeLabel.get(),
                "oe.processor.100.scrubber.playback_start",
                "Playback start",
                "Configured start time of playback.",
                "00:00:04.000" },
            ExpectedLabel {
                scrubber.fullMiddleTimeLabel.get(),
                "oe.processor.100.scrubber.playback_position",
                "Playback position",
                "Current playback position in the recording.",
                "00:00:05.000" },
            ExpectedLabel {
                scrubber.fullEndTimeLabel.get(),
                "oe.processor.100.scrubber.playback_end",
                "Playback end",
                "Configured end time of playback.",
                "00:00:06.000" },
            ExpectedLabel {
                scrubber.minStartTimeLabel.get(),
                "oe.processor.100.scrubber.minimum_start",
                "Minimum playback start",
                "Earliest available time in the recording.",
                "00:00:00.000" },
            ExpectedLabel {
                scrubber.maxEndTimeLabel.get(),
                "oe.processor.100.scrubber.maximum_end",
                "Maximum playback end",
                "Latest available time in the recording.",
                "00:00:08.000" }
        };

    for (const auto& expected :
         expectedLabels)
    {
        ASSERT_NE (expected.label, nullptr);
        expected.label->setText (
            expected.value,
            dontSendNotification);

        EXPECT_EQ (
            expected.label->getComponentID(),
            expected.id);
        EXPECT_EQ (
            expected.label->getTitle(),
            expected.title);
        EXPECT_EQ (
            expected.label->getDescription(),
            expected.description);

        auto handler =
            expected.label
                ->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::staticText);
        ASSERT_NE (
            handler->getValueInterface(),
            nullptr);
        EXPECT_TRUE (
            handler->getValueInterface()
                ->isReadOnly());
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            expected.value);
    }
}

TEST_F (FileReaderAccessibilityTests,
        ExposesWritableScrubberTimelines)
{
    FileReader reader;
    reader.setNodeId (100);
    ScrubberInterface scrubber (&reader);

    auto* fullTimeline =
        static_cast<FullTimeline*> (
            scrubber.fullTimeline.get());
    auto* zoomTimeline =
        static_cast<ZoomTimeline*> (
            scrubber.zoomTimeline.get());

    ASSERT_NE (fullTimeline, nullptr);
    ASSERT_NE (zoomTimeline, nullptr);

    fullTimeline->setStartStopTimes (0, 60000);
    zoomTimeline->setStartStopTimes (0, 60000);
    fullTimeline->setIntervalPosition (0);

    EXPECT_EQ (
        fullTimeline->getComponentID(),
        "oe.processor.100.scrubber.full_timeline");
    EXPECT_EQ (
        fullTimeline->getTitle(),
        "Zoom window position");
    EXPECT_EQ (
        fullTimeline->getDescription(),
        "Move the 30-second zoom window through the configured playback range.");

    auto fullHandler =
        fullTimeline->createAccessibilityHandler();
    ASSERT_NE (fullHandler, nullptr);
    EXPECT_EQ (
        fullHandler->getRole(),
        AccessibilityRole::slider);

    auto* fullValue =
        fullHandler->getValueInterface();
    ASSERT_NE (fullValue, nullptr);
    EXPECT_FALSE (fullValue->isReadOnly());
    EXPECT_TRUE (fullValue->getRange().isValid());
    EXPECT_DOUBLE_EQ (
        fullValue->getRange().getMinimumValue(),
        0.0);
    EXPECT_DOUBLE_EQ (
        fullValue->getRange().getMaximumValue(),
        1.0);
    EXPECT_NEAR (
        fullValue->getRange().getInterval(),
        1.0 / 180.0,
        0.000001);

    fullValue->setValue (0.5);
    EXPECT_EQ (
        fullTimeline->getStartInterval(),
        90);
    EXPECT_DOUBLE_EQ (
        fullValue->getCurrentValue(),
        0.5);
    fullValue->setValue (2.0);
    EXPECT_EQ (
        fullTimeline->getStartInterval(),
        180);
    EXPECT_DOUBLE_EQ (
        fullValue->getCurrentValue(),
        1.0);

    EXPECT_EQ (
        zoomTimeline->getComponentID(),
        "oe.processor.100.scrubber.zoom_timeline");
    EXPECT_EQ (
        zoomTimeline->getTitle(),
        "Playback position in zoom window");
    EXPECT_EQ (
        zoomTimeline->getDescription(),
        "Move the file-reader playhead within the current zoom window.");

    auto zoomHandler =
        zoomTimeline->createAccessibilityHandler();
    ASSERT_NE (zoomHandler, nullptr);
    EXPECT_EQ (
        zoomHandler->getRole(),
        AccessibilityRole::slider);

    auto* zoomValue =
        zoomHandler->getValueInterface();
    ASSERT_NE (zoomValue, nullptr);
    EXPECT_FALSE (zoomValue->isReadOnly());
    EXPECT_TRUE (zoomValue->getRange().isValid());
    EXPECT_DOUBLE_EQ (
        zoomValue->getRange().getMinimumValue(),
        0.0);
    EXPECT_DOUBLE_EQ (
        zoomValue->getRange().getMaximumValue(),
        1.0);
    EXPECT_NEAR (
        zoomValue->getRange().getInterval(),
        1.0 / 352.0,
        0.000001);

    EXPECT_FLOAT_EQ (
        reader.getCurrentSampleRate(),
        0.0f);
    zoomValue->setValue (0.75);
    EXPECT_EQ (
        zoomTimeline->getSliderPosition(),
        264);
    EXPECT_DOUBLE_EQ (
        zoomValue->getCurrentValue(),
        0.75);
    zoomValue->setValue (-1.0);
    EXPECT_EQ (
        zoomTimeline->getSliderPosition(),
        0);
    EXPECT_DOUBLE_EQ (
        zoomValue->getCurrentValue(),
        0.0);
}

TEST_F (FileReaderAccessibilityTests,
        TimelineWriteWithAnEditorDoesNotRequireAFileOrMovePlaybackStart)
{
    FileReader reader;
    reader.setNodeId (100);
    reader.registerParameters();
    ASSERT_NE (
        reader.createEditor(),
        nullptr);

    auto* scrubber =
        reader.getScrubberInterface();
    ASSERT_NE (scrubber, nullptr);

    auto* fullTimeline =
        static_cast<FullTimeline*> (
            scrubber->fullTimeline.get());
    ASSERT_NE (fullTimeline, nullptr);

    fullTimeline->setStartStopTimes (
        0,
        60000);
    fullTimeline->setIntervalPosition (0);

    const auto originalPlaybackStart =
        reader.getPlaybackStart();
    const auto originalCurrentSample =
        reader.getCurrentSample();

    auto handler =
        fullTimeline
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);

    handler->getValueInterface()
        ->setValue (0.5);
    scrubber->setCurrentSample (100);
    scrubber->updatePlaybackTimes();

    EXPECT_EQ (
        reader.getPlaybackStart(),
        originalPlaybackStart);
    EXPECT_EQ (
        reader.getCurrentSample(),
        originalCurrentSample);
    EXPECT_DOUBLE_EQ (
        handler->getValueInterface()
            ->getCurrentValue(),
        0.5);
}
