#include "../../Source/Processors/Editors/DelayMonitor.h"
#include "gtest/gtest.h"

TEST (DelayMonitorAccessibilityTests, PublishesFormattedDelayAsReadOnlyState)
{
    DelayMonitor monitor;

    EXPECT_EQ (monitor.getComponentID(), "oe.status.processing_delay");
    EXPECT_EQ (monitor.getTitle(), "Processing delay");
    EXPECT_EQ (monitor.getDescription(),
               "Elapsed processing time for this data stream.");

    auto handler = monitor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::staticText);

    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    EXPECT_TRUE (value->isReadOnly());
    EXPECT_EQ (value->getCurrentValueAsString(), "0.00 ms");

    monitor.setDelay (1.25f);

    EXPECT_EQ (value->getCurrentValueAsString(), "1.25 ms");
}

TEST (DelayMonitorAccessibilityTests, ScopesPublishedMeaningToItsDataStream)
{
    DelayMonitor monitor;

    monitor.setAccessibilityContext (
        "oe.processor.100.streams.stream_probe_ap.processing_delay",
        "Probe AP processing delay",
        "Processing delay for Probe AP.");

    EXPECT_EQ (
        monitor.getComponentID(),
        "oe.processor.100.streams.stream_probe_ap.processing_delay");
    EXPECT_EQ (monitor.getTitle(), "Probe AP processing delay");
    EXPECT_EQ (monitor.getDescription(), "Processing delay for Probe AP.");
}
