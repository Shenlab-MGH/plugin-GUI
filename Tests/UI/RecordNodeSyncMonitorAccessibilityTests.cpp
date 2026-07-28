#include "../../Source/Processors/RecordNode/RecordNodeEditor.h"
#include "gtest/gtest.h"
#include <thread>

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

TEST (RecordNodeSyncMonitorAccessibilityTests,
      PublishesWorkerSafeProviderAndEnabledState)
{
    SyncStartTimeMonitor monitor;
    monitor.setAccessibilityContext (
        "oe.processor.100.streams.stream_probe_ap.sync_start_offset",
        "Probe AP synchronization start offset",
        "Synchronization start offset for Probe AP.");
    monitor.setSyncMetric (true, 2.5f);
    monitor.setEnabled (false);

    auto handler = monitor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_NE (handler->getValueInterface(), nullptr);

    EXPECT_FALSE (
        static_cast<Component&> (monitor).isEnabled());
    EXPECT_FALSE (handler->isEnabled());

    String workerTitle;
    String workerDescription;
    String workerHelp;
    String workerValue;
    AccessibleState workerState;
    bool workerEnabled = true;
    std::thread reader (
        [&]
        {
            workerTitle = handler->getTitle();
            workerDescription =
                handler->getDescription();
            workerHelp = handler->getHelp();
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
            workerState = handler->getCurrentState();
            workerEnabled = handler->isEnabled();
        });
    reader.join();

    EXPECT_EQ (
        workerTitle,
        "Probe AP synchronization start offset");
    EXPECT_EQ (
        workerDescription,
        "Synchronization start offset for Probe AP.");
    EXPECT_EQ (workerHelp, workerDescription);
    EXPECT_EQ (workerValue, "2.50 ms");
    EXPECT_TRUE (workerState.isFocusable());
    EXPECT_FALSE (workerEnabled);
}

TEST (RecordNodeSyncMonitorAccessibilityTests,
      TracksEffectiveComponentEnablement)
{
    Component parent;
    SyncAccuracyMonitor monitor;
    parent.addAndMakeVisible (monitor);
    auto handler = monitor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    EXPECT_TRUE (handler->isEnabled());

    static_cast<Component&> (monitor).setEnabled (false);
    EXPECT_FALSE (handler->isEnabled());

    static_cast<Component&> (monitor).setEnabled (true);
    EXPECT_TRUE (handler->isEnabled());

    parent.setEnabled (false);
    EXPECT_FALSE (monitor.Component::isEnabled());
    EXPECT_FALSE (handler->isEnabled());

    parent.setEnabled (true);
    EXPECT_TRUE (monitor.Component::isEnabled());
    EXPECT_TRUE (handler->isEnabled());
}

TEST (RecordNodeSyncMonitorAccessibilityTests,
      ExistingHandlerTracksMetadataInvalidation)
{
    SyncAccuracyMonitor monitor;
    monitor.addToDesktop (0);
    auto existingHandler =
        monitor.createAccessibilityHandler();
    ASSERT_NE (existingHandler, nullptr);
    ASSERT_NE (
        monitor.getAccessibilityHandler(),
        nullptr);

    monitor.setAccessibilityContext (
        "oe.processor.100.streams.stream_probe_ap.sync_accuracy",
        "Probe AP synchronization accuracy",
        "Synchronization accuracy for Probe AP.");
    monitor.setSyncMetric (true, -0.25f);
    auto* replacementHandler =
        monitor.getAccessibilityHandler();
    ASSERT_NE (replacementHandler, nullptr);

    String existingTitle;
    String existingValue;
    String replacementTitle;
    String replacementValue;
    std::thread reader (
        [&]
        {
            existingTitle =
                existingHandler->getTitle();
            existingValue =
                existingHandler->getValueInterface()
                    ->getCurrentValueAsString();
            replacementTitle =
                replacementHandler->getTitle();
            replacementValue =
                replacementHandler->getValueInterface()
                    ->getCurrentValueAsString();
        });
    reader.join();

    EXPECT_EQ (
        existingTitle,
        "Probe AP synchronization accuracy");
    EXPECT_EQ (
        replacementTitle,
        existingTitle);
    EXPECT_EQ (existingValue, "0.250 ms");
    EXPECT_EQ (replacementValue, existingValue);
}
