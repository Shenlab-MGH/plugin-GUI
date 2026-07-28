#include "../../Source/Processors/FileReader/FileReader.h"
#include "../../Source/Processors/FileReader/ScrubberInterface.h"
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
    }

    void TearDown() override
    {
        messageManagerLock.reset();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<MessageManagerLock>
        messageManagerLock;
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
