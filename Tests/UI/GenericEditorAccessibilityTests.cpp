#include "../../Source/Processors/Editors/GenericEditor.h"
#include "../../Source/Processors/Editors/StreamSelector.h"
#include "../../Source/Processors/GenericProcessor/GenericProcessor.h"
#include "../../Source/Processors/Settings/DataStream.h"
#include "gtest/gtest.h"

namespace
{
class TestStreamProcessor final : public GenericProcessor
{
public:
    TestStreamProcessor() : GenericProcessor ("Test Processor") {}

    void process (AudioBuffer<float>&) override {}
};

class InspectableGenericEditor final : public GenericEditor
{
public:
    explicit InspectableGenericEditor (GenericProcessor* processor)
        : GenericEditor (processor)
    {
    }

    StreamSelectorTable& getStreamSelector() { return *streamSelector; }
};

Component* findDescendantBySemanticId (Component& parent, const String& id)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getComponentID() == id)
            return child;

        if (auto* descendant = findDescendantBySemanticId (*child, id))
            return descendant;
    }

    return nullptr;
}
} // namespace

TEST (GenericEditorAccessibilityTests, ExposesProcessorDrawerAsStableToggle)
{
    DrawerButton drawer ("File Reader (100) Drawer Button",
                         "oe.processor.100.drawer",
                         "File Reader controls",
                         "Show or hide controls for File Reader processor 100.");

    EXPECT_EQ (drawer.getComponentID(), "oe.processor.100.drawer");
    EXPECT_EQ (drawer.getTitle(), "File Reader controls");
    EXPECT_EQ (drawer.getDescription(), "Show or hide controls for File Reader processor 100.");
    EXPECT_TRUE (drawer.isAccessible());
    EXPECT_TRUE (drawer.getClickingTogglesState());
}

TEST (GenericEditorAccessibilityTests, ExposesStreamSelectorNavigationControls)
{
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (&processor);
    auto& selector = editor.getStreamSelector();

    EXPECT_EQ (selector.getComponentID(), "oe.processor.100.streams");
    EXPECT_EQ (selector.getTitle(), "Test Processor data streams");
    EXPECT_EQ (selector.getDescription(),
               "Select and inspect data streams for Test Processor (node 100).");

    auto* table = findDescendantBySemanticId (
        selector,
        "oe.processor.100.streams.table");
    ASSERT_NE (table, nullptr);
    EXPECT_EQ (table->getTitle(), "Available data streams");
    EXPECT_EQ (table->getDescription(),
               "Select the data stream shown by Test Processor (node 100).");

    auto* expand = findDescendantBySemanticId (
        selector,
        "oe.processor.100.streams.expand");
    ASSERT_NE (expand, nullptr);
    EXPECT_EQ (expand->getTitle(), "Expand data streams");
    EXPECT_EQ (expand->getDescription(),
               "Open the full data stream table for Test Processor (node 100).");
    auto expandHandler = expand->createAccessibilityHandler();
    ASSERT_NE (expandHandler, nullptr);
    EXPECT_EQ (expandHandler->getRole(), AccessibilityRole::button);
}

TEST (GenericEditorAccessibilityTests, ScopesStreamStatusControlsToProcessorAndStream)
{
    TestStreamProcessor processor;
    processor.setNodeId (100);
    InspectableGenericEditor editor (&processor);
    auto& selector = editor.getStreamSelector();
    DataStream stream ({ "Probe AP",
                         "Neuropixels action-potential stream",
                         "probe.ap",
                         30000.0f,
                         true });

    selector.add (&stream);
    selector.finishedUpdate();

    auto* ttlMonitor = selector.getTTLMonitor (&stream);
    ASSERT_NE (ttlMonitor, nullptr);

    const String expectedId =
        "oe.processor.100.streams.stream_probe_ap.ttl_lines";
    EXPECT_EQ (ttlMonitor->getComponentID(), expectedId);
    EXPECT_EQ (ttlMonitor->getTitle(), "Probe AP TTL line states");
    EXPECT_EQ (ttlMonitor->getDescription(),
               "Current digital event state for Probe AP.");
    EXPECT_EQ (ttlMonitor->getChildComponent (0)->getComponentID(),
               expectedId + ".line_1");
}
