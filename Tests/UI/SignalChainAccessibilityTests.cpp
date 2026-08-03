#include "../../Source/UI/EditorViewport.h"
#include "gtest/gtest.h"

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
