#include "../../Source/Processors/RecordNode/RecordNodeEditor.h"
#include "gtest/gtest.h"

TEST (RecordNodeSyncMonitorAccessibilityTests, PublishesStartTimeOffset)
{
    SyncStartTimeMonitor monitor;

    EXPECT_EQ (monitor.getComponentID(), "oe.status.sync_start_offset");
    EXPECT_EQ (monitor.getTitle(), "Synchronization start offset");

    auto handler = monitor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::staticText);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    EXPECT_TRUE (value->isReadOnly());
    EXPECT_EQ (value->getCurrentValueAsString(), "--");

    monitor.setSyncMetric (true, 1.25f);

    EXPECT_EQ (value->getCurrentValueAsString(), "1.25 ms");
}

TEST (RecordNodeSyncMonitorAccessibilityTests, PublishesLastEventAge)
{
    LastSyncEventMonitor monitor;
    auto handler = monitor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);

    monitor.setSyncMetric (true, 30.0f);
    EXPECT_EQ (value->getCurrentValueAsString(), "<1 min");
    monitor.setSyncMetric (true, 120.0f);
    EXPECT_EQ (value->getCurrentValueAsString(), ">1 min");
    monitor.setSyncMetric (true, 600.0f);
    EXPECT_EQ (value->getCurrentValueAsString(), ">5 min");
    monitor.setSyncMetric (true, -1.0f);
    EXPECT_EQ (value->getCurrentValueAsString(), "--");
}

TEST (RecordNodeSyncMonitorAccessibilityTests, PublishesAbsoluteSyncAccuracy)
{
    SyncAccuracyMonitor monitor;
    auto handler = monitor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);

    monitor.setSyncMetric (true, -0.1254f);

    EXPECT_EQ (value->getCurrentValueAsString(), "0.125 ms");
}

TEST (RecordNodeSyncMonitorAccessibilityTests, ScopesMeaningToItsDataStream)
{
    SyncStartTimeMonitor monitor;

    monitor.setAccessibilityContext (
        "oe.processor.100.streams.stream_probe_ap.sync_start_offset",
        "Probe AP synchronization start offset",
        "Synchronization start offset for Probe AP.");

    EXPECT_EQ (
        monitor.getComponentID(),
        "oe.processor.100.streams.stream_probe_ap.sync_start_offset");
    EXPECT_EQ (monitor.getTitle(),
               "Probe AP synchronization start offset");
    EXPECT_EQ (monitor.getDescription(),
               "Synchronization start offset for Probe AP.");
}
