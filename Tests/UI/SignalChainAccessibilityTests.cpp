#include "../../Source/UI/EditorViewport.h"
#include "../../Source/Processors/EmptyProcessor/EmptyProcessor.h"
#include "gtest/gtest.h"

#include <optional>
#include <vector>

namespace
{
class AccessibilityTestProcessor : public GenericProcessor
{
public:
    explicit AccessibilityTestProcessor (const String& canonicalName)
        : GenericProcessor (canonicalName, true)
    {
    }

    void process (AudioBuffer<float>&) override {}
};
} // namespace

TEST (SignalChainAccessibilityTests, ExposesEditorViewportAsTheLoadedProcessorList)
{
    SignalChainTabComponent tabs;
    auto* viewport = new EditorViewport (&tabs);

    EXPECT_EQ (viewport->getComponentID(), "oe.control.signal_chain.processors");
    EXPECT_EQ (viewport->getTitle(), "Loaded processors");
    EXPECT_EQ (viewport->getDescription(), "Read-only inventory of processors loaded in the signal chain.");
    EXPECT_TRUE (viewport->isAccessible());

    auto handler = viewport->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::list);
}

TEST (SignalChainAccessibilityTests, ExposesConstructorNameAndSessionNodeWhileKeepingDisplayNameDistinct)
{
    AccessibilityTestProcessor predecessor ("File Reader");
    predecessor.setNodeId (11);

    AccessibilityTestProcessor processor ("Bandpass Filter");
    processor.setNodeId (22);
    processor.setSourceNode (&predecessor);

    GenericEditor editor (&processor);
    EXPECT_EQ (editor.getComponentID(), "oe.processor.22");
    EXPECT_EQ (editor.getTitle(), "Bandpass Filter");
    EXPECT_TRUE (editor.getDescription().contains ("Constructor-time processor name: Bandpass Filter."));
    EXPECT_TRUE (editor.getDescription().contains ("Node ID: 22."));
    EXPECT_FALSE (editor.getDescription().containsIgnoreCase ("predecessor"));
    EXPECT_TRUE (editor.getDescription().contains ("Display name matches constructor-time processor name."));

    editor.setDisplayName ("Mouse probe filter");

    EXPECT_EQ (editor.getComponentID(), "oe.processor.22");
    EXPECT_EQ (editor.getTitle(), "Mouse probe filter");
    EXPECT_TRUE (editor.getDescription().contains ("Constructor-time processor name: Bandpass Filter."));
    EXPECT_TRUE (editor.getDescription().contains ("Display name: Mouse probe filter."));

    auto handler = editor.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::listItem);
}

TEST (SignalChainAccessibilityTests, UsesSessionNodeIdToDisambiguateEqualNamesAndKeepsIdStableAcrossRename)
{
    AccessibilityTestProcessor firstProcessor ("Record Node");
    firstProcessor.setNodeId (101);
    AccessibilityTestProcessor secondProcessor ("Record Node");
    secondProcessor.setNodeId (202);

    GenericEditor first (&firstProcessor);
    GenericEditor second (&secondProcessor);
    EXPECT_EQ (first.getComponentID(), "oe.processor.101");
    EXPECT_EQ (second.getComponentID(), "oe.processor.202");
    EXPECT_NE (first.getComponentID(), second.getComponentID());

    first.setDisplayName ("Mouse 7 recorder");
    EXPECT_EQ (first.getTitle(), "Mouse 7 recorder");
    EXPECT_EQ (first.getComponentID(), "oe.processor.101");
}

TEST (SignalChainAccessibilityTests, BuildsValueOnlySnapshotInApiOrderWhileSkippingPlaceholdersAndDuplicates)
{
    // This real production type crosses the gui_testable_source boundary. Its
    // class declaration must use TESTABLE so this contract is portable beyond MSVC.
    AccessibilityTestProcessor firstRoot ("Probe 1");
    AccessibilityTestProcessor sharedDownstream ("Shared record node");
    EmptyProcessor placeholder;
    AccessibilityTestProcessor eighthRoot ("Probe 8");
    firstRoot.setNodeId (101);
    sharedDownstream.setNodeId (303);
    placeholder.setProcessorType (Plugin::Processor::EMPTY);
    placeholder.setNodeId (777);
    ASSERT_TRUE (placeholder.isEmpty());
    eighthRoot.setNodeId (801);
    sharedDownstream.setSourceNode (&firstRoot);

    const Array<GenericProcessor*> apiOrder {
        &eighthRoot, &sharedDownstream, &placeholder, &firstRoot, &sharedDownstream
    };
    const auto snapshot = buildProcessorAccessibilitySnapshot (apiOrder);

    ASSERT_EQ (snapshot.size(), 3u);
    EXPECT_EQ (snapshot[0].nodeId, 801);
    EXPECT_EQ (snapshot[0].name, "Probe 8");
    EXPECT_EQ (snapshot[0].predecessorNodeId, std::nullopt);
    EXPECT_EQ (snapshot[1].nodeId, 303);
    EXPECT_EQ (snapshot[1].name, "Shared record node");
    ASSERT_TRUE (snapshot[1].predecessorNodeId.has_value());
    EXPECT_EQ (*snapshot[1].predecessorNodeId, 101);
    EXPECT_EQ (snapshot[2].nodeId, 101);
    EXPECT_EQ (snapshot[2].name, "Probe 1");
    EXPECT_EQ (snapshot[2].predecessorNodeId, std::nullopt);
}

TEST (SignalChainAccessibilityTests,
      RefreshesVisibleGenericEditorPredecessorWhilePreservingRicherDescriptionAndTitle)
{
    AccessibilityTestProcessor processor ("Bandpass Filter");
    processor.setNodeId (22);
    GenericEditor editor (&processor);
    editor.setDisplayName ("Mouse probe filter");

    const auto richDescriptionSeed = editor.getDescription();
    ASSERT_TRUE (richDescriptionSeed.contains ("Constructor-time processor name: Bandpass Filter."));
    ASSERT_TRUE (richDescriptionSeed.contains ("Display name: Mouse probe filter."));
    ASSERT_FALSE (richDescriptionSeed.contains ("Predecessor node ID:"));
    ASSERT_EQ (editor.getTitle(), "Mouse probe filter");

    SignalChainTabComponent tabs;
    auto* viewport = new EditorViewport (&tabs);
    viewport->updateVisibleEditors (Array<GenericEditor*> { &editor }, 1, 0);

    viewport->updateAccessibleProcessorInventory (
        { { 22, "Mouse probe filter", 11 } });

    EXPECT_EQ (editor.getTitle(), "Mouse probe filter");
    EXPECT_EQ (editor.getComponentID(), "oe.processor.22");
    EXPECT_TRUE (editor.getDescription().contains (
        "Constructor-time processor name: Bandpass Filter."));
    EXPECT_TRUE (editor.getDescription().contains ("Display name: Mouse probe filter."));
    EXPECT_TRUE (editor.getDescription().contains ("Predecessor node ID: 11."));
    EXPECT_FALSE (editor.getDescription().contains ("Predecessor node ID: none."));

    // Same node, mutated predecessor only: replace the clause, keep richer text/title.
    viewport->updateAccessibleProcessorInventory (
        { { 22, "Mouse probe filter", 99 } });

    EXPECT_EQ (editor.getTitle(), "Mouse probe filter");
    EXPECT_EQ (editor.getComponentID(), "oe.processor.22");
    EXPECT_TRUE (editor.getDescription().contains (
        "Constructor-time processor name: Bandpass Filter."));
    EXPECT_TRUE (editor.getDescription().contains ("Display name: Mouse probe filter."));
    EXPECT_TRUE (editor.getDescription().contains ("Predecessor node ID: 99."));
    EXPECT_FALSE (editor.getDescription().contains ("Predecessor node ID: 11."));

    viewport->updateAccessibleProcessorInventory (
        { { 22, "Mouse probe filter", std::nullopt } });

    EXPECT_EQ (editor.getTitle(), "Mouse probe filter");
    EXPECT_TRUE (editor.getDescription().contains (
        "Constructor-time processor name: Bandpass Filter."));
    EXPECT_TRUE (editor.getDescription().contains ("Display name: Mouse probe filter."));
    EXPECT_TRUE (editor.getDescription().contains ("Predecessor node ID: none."));
    EXPECT_FALSE (editor.getDescription().contains ("Predecessor node ID: 99."));
}

TEST (SignalChainAccessibilityTests, UpdatesProxyPredecessorDescriptionOnInventoryMutation)
{
    SignalChainTabComponent tabs;
    auto* viewport = new EditorViewport (&tabs);

    viewport->updateAccessibleProcessorInventory (
        { { 801, "Probe 8", std::nullopt } });

    auto traverser = viewport->createFocusTraverser();
    ASSERT_NE (traverser, nullptr);
    auto* proxy = traverser->getDefaultComponent (viewport);
    ASSERT_NE (proxy, nullptr);
    EXPECT_EQ (proxy->getComponentID(), "oe.processor.801");
    EXPECT_EQ (proxy->getTitle(), "Probe 8");
    EXPECT_TRUE (proxy->getDescription().contains ("Node ID: 801."));
    EXPECT_TRUE (proxy->getDescription().contains ("Predecessor node ID: none."));

    viewport->updateAccessibleProcessorInventory (
        { { 801, "Probe 8", 100 } });

    traverser = viewport->createFocusTraverser();
    proxy = traverser->getDefaultComponent (viewport);
    ASSERT_NE (proxy, nullptr);
    EXPECT_EQ (proxy->getComponentID(), "oe.processor.801");
    EXPECT_EQ (proxy->getTitle(), "Probe 8");
    EXPECT_TRUE (proxy->getDescription().contains ("Node ID: 801."));
    EXPECT_TRUE (proxy->getDescription().contains ("Predecessor node ID: 100."));
    EXPECT_FALSE (proxy->getDescription().contains ("Predecessor node ID: none."));
}
