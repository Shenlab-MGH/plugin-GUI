#include "../../Source/UI/EditorViewport.h"
#include "gtest/gtest.h"

#include <UIAutomation.h>
#include <combaseapi.h>
#include <oleauto.h>

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

namespace
{
class SmokeTestProcessor : public GenericProcessor
{
public:
    SmokeTestProcessor() : GenericProcessor ("Record Node", true) {}
    void process (AudioBuffer<float>&) override {}
};

class ComApartment
{
public:
    ComApartment() : result (CoInitializeEx (nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment()
    {
        if (SUCCEEDED (result))
            CoUninitialize();
    }
    HRESULT result;
};

template <typename Interface>
class ComOwner
{
public:
    ~ComOwner()
    {
        if (value != nullptr)
            value->Release();
    }
    Interface** put() { return &value; }
    Interface* get() const { return value; }
    Interface* operator->() const { return value; }

private:
    Interface* value = nullptr;
};

HRESULT findByAutomationId (IUIAutomation* automation,
                            IUIAutomationElement* searchRoot,
                            const wchar_t* automationId,
                            TreeScope scope,
                            IUIAutomationElement** result)
{
    VARIANT value;
    VariantInit (&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString (automationId);
    if (value.bstrVal == nullptr)
        return E_OUTOFMEMORY;

    ComOwner<IUIAutomationCondition> condition;
    const auto conditionResult = automation->CreatePropertyCondition (
        UIA_AutomationIdPropertyId, value, condition.put());
    VariantClear (&value);
    if (FAILED (conditionResult))
        return conditionResult;

    return searchRoot->FindFirst (scope, condition.get(), result);
}

struct ProcessorItemObservation
{
    String automationId;
    String name;
    String fullDescription;
    bool isOffscreen = false;
    bool invokePatternAvailable = false;
};

struct UiaObservation
{
    HRESULT result = E_FAIL;
    CONTROLTYPEID rootType = 0;
    CONTROLTYPEID itemType = 0;
    String itemName;
    bool rootFound = false;
    bool itemFound = false;
    bool invokePatternAvailable = false;
    bool directChildrenAreOnlyProcessorItems = false;
    int directChildCount = 0;
    StringArray directProcessorAutomationIds;
    std::vector<ProcessorItemObservation> directProcessorItems;
    bool firstRootEditorDescendantFound = false;
    bool eighthRootEditorDescendantFound = false;
};

String describeObservation (const UiaObservation& observation)
{
    return "hr=0x" + String::toHexString ((int) observation.result)
         + " rootFound=" + String (observation.rootFound ? "true" : "false")
         + " itemFound=" + String (observation.itemFound ? "true" : "false")
         + " directChildCount=" + String (observation.directChildCount)
         + " directIds=" + observation.directProcessorAutomationIds.joinIntoString (",");
}

UiaObservation observeProcessorInventory (HWND windowHandle)
{
    UiaObservation observation;
    ComApartment apartment;
    observation.result = apartment.result;
    if (FAILED (observation.result))
        return observation;

    ComOwner<IUIAutomation> automation;
    observation.result = CoCreateInstance (CLSID_CUIAutomation, nullptr,
                                           CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS (automation.put()));
    if (FAILED (observation.result))
        return observation;

    ComOwner<IUIAutomationElement> nativeWindow;
    observation.result = automation->ElementFromHandle (windowHandle, nativeWindow.put());
    if (FAILED (observation.result))
        return observation;

    ComOwner<IUIAutomationElement> root;
    observation.result = findByAutomationId (automation.get(), nativeWindow.get(),
                                             L"oe.control.signal_chain.processors",
                                             TreeScope_Subtree, root.put());
    if (FAILED (observation.result) || root.get() == nullptr)
        return observation;
    observation.rootFound = true;
    observation.result = root->get_CurrentControlType (&observation.rootType);
    if (FAILED (observation.result))
        return observation;

    ComOwner<IUIAutomationCondition> trueCondition;
    observation.result = automation->CreateTrueCondition (trueCondition.put());
    if (FAILED (observation.result))
        return observation;

    // Healthy path: query only direct semantic children (no full-tree diagnostic crawl).
    ComOwner<IUIAutomationElementArray> directChildren;
    observation.result = root->FindAll (TreeScope_Children, trueCondition.get(), directChildren.put());
    if (FAILED (observation.result) || directChildren.get() == nullptr)
        return observation;
    int directChildCount = 0;
    observation.result = directChildren->get_Length (&directChildCount);
    observation.directChildCount = directChildCount;
    if (FAILED (observation.result))
        return observation;
    if (directChildCount > 0)
        observation.directChildrenAreOnlyProcessorItems = true;
    for (int index = 0; index < directChildCount; ++index)
    {
        ComOwner<IUIAutomationElement> element;
        BSTR id = nullptr;
        CONTROLTYPEID type = 0;
        if (FAILED (directChildren->GetElement (index, element.put()))
            || FAILED (element->get_CurrentAutomationId (&id))
            || FAILED (element->get_CurrentControlType (&type)))
        {
            SysFreeString (id);
            observation.directChildrenAreOnlyProcessorItems = false;
            break;
        }
        const auto semanticId = String (id);
        SysFreeString (id);
        if (! semanticId.startsWith ("oe.processor.") || type != UIA_ListItemControlTypeId)
        {
            observation.directChildrenAreOnlyProcessorItems = false;
            break;
        }
        observation.directProcessorAutomationIds.add (semanticId);

        ProcessorItemObservation itemObservation;
        itemObservation.automationId = semanticId;
        BSTR name = nullptr;
        if (SUCCEEDED (element->get_CurrentName (&name)) && name != nullptr)
            itemObservation.name = String (name);
        SysFreeString (name);

        VARIANT fullDescription;
        VariantInit (&fullDescription);
        if (SUCCEEDED (element->GetCurrentPropertyValue (
                UIA_FullDescriptionPropertyId, &fullDescription))
            && fullDescription.vt == VT_BSTR && fullDescription.bstrVal != nullptr)
            itemObservation.fullDescription = String (fullDescription.bstrVal);
        VariantClear (&fullDescription);

        BOOL isOffscreen = FALSE;
        if (SUCCEEDED (element->get_CurrentIsOffscreen (&isOffscreen)))
            itemObservation.isOffscreen = isOffscreen != FALSE;
        ComOwner<IUnknown> invoke;
        if (SUCCEEDED (element->GetCurrentPattern (UIA_InvokePatternId, invoke.put())))
            itemObservation.invokePatternAvailable = invoke.get() != nullptr;
        observation.directProcessorItems.push_back (std::move (itemObservation));
    }

    ComOwner<IUIAutomationElement> firstRootEditorDescendant;
    if (SUCCEEDED (observation.result))
        observation.result = findByAutomationId (
            automation.get(), root.get(), L"oe.test.root1.editor.descendant",
            TreeScope_Subtree, firstRootEditorDescendant.put());
    observation.firstRootEditorDescendantFound = firstRootEditorDescendant.get() != nullptr;
    ComOwner<IUIAutomationElement> eighthRootEditorDescendant;
    if (SUCCEEDED (observation.result))
        observation.result = findByAutomationId (
            automation.get(), root.get(), L"oe.test.root8.editor.descendant",
            TreeScope_Subtree, eighthRootEditorDescendant.put());
    observation.eighthRootEditorDescendantFound = eighthRootEditorDescendant.get() != nullptr;

    ComOwner<IUIAutomationElement> item;
    observation.result = findByAutomationId (automation.get(), root.get(),
                                             L"oe.processor.4242",
                                             TreeScope_Children, item.put());
    if (FAILED (observation.result) || item.get() == nullptr)
        return observation;
    observation.itemFound = true;
    observation.result = item->get_CurrentControlType (&observation.itemType);
    if (FAILED (observation.result))
        return observation;

    BSTR name = nullptr;
    observation.result = item->get_CurrentName (&name);
    if (SUCCEEDED (observation.result) && name != nullptr)
        observation.itemName = String (name);
    SysFreeString (name);
    if (FAILED (observation.result))
        return observation;

    ComOwner<IUnknown> invoke;
    observation.result = item->GetCurrentPattern (UIA_InvokePatternId, invoke.put());
    observation.invokePatternAvailable = invoke.get() != nullptr;
    return observation;
}

UiaObservation observeWhilePumpingMessages (HWND windowHandle)
{
    UiaObservation observation;
    std::atomic<bool> finished { false };
    std::thread worker ([&]
                        {
                            observation = observeProcessorInventory (windowHandle);
                            finished.store (true);
                        });

    const auto deadline = Time::getMillisecondCounter() + 3000;
    bool timeoutReported = false;
    while (! finished.load())
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);
        if (! timeoutReported && Time::getMillisecondCounter() >= deadline)
        {
            ADD_FAILURE() << "Timed out waiting for the Windows UI Automation worker.";
            timeoutReported = true;
        }
    }
    worker.join();
    return observation;
}

class ProcessorInventoryWindowsUIAutomationTests : public testing::Test
{
protected:
    // Keep MessageManager / UIA provider process-stable across TEST_F cases.
    // Per-test deleteInstance/DeletedAtShutdown teardown is the instability root
    // (v1.1 449f96f removed it for the same reason).
    void SetUp() override
    {
        MessageManager::getInstance();
    }
};
} // namespace

TEST_F (ProcessorInventoryWindowsUIAutomationTests, ExternalClientFindsReadOnlyRootAndDynamicProcessorItem)
{
    SmokeTestProcessor processor;
    processor.setNodeId (4242);
    GenericEditor editor (&processor);
    editor.setDisplayName ("Mouse 8 recorder");

    auto tabs = std::make_unique<SignalChainTabComponent>();
    auto* viewport = new EditorViewport (tabs.get());

    DocumentWindow window ("Processor inventory UIA smoke", Colours::black, 0);
    window.setContentOwned (tabs.release(), true);
    window.centreWithSize (700, 300);
    window.setVisible (true);
    ASSERT_NE (window.getPeer(), nullptr);
    MessageManager::getInstance()->runDispatchLoopUntil (100);

    Array<GenericEditor*> visibleEditors { &editor };
    viewport->updateVisibleEditors (visibleEditors, 1, 0);
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    ASSERT_EQ (editor.getParentComponent(), viewport);
    ASSERT_TRUE (editor.isShowing());
    ASSERT_FALSE (editor.getBounds().isEmpty());

    const auto observation = observeWhilePumpingMessages (
        static_cast<HWND> (window.getPeer()->getNativeHandle()));
    ASSERT_TRUE (SUCCEEDED (observation.result)) << describeObservation (observation);
    EXPECT_TRUE (observation.rootFound) << describeObservation (observation);
    EXPECT_TRUE (observation.itemFound) << describeObservation (observation);
    EXPECT_EQ (observation.rootType, UIA_ListControlTypeId);
    EXPECT_EQ (observation.itemType, UIA_ListItemControlTypeId);
    EXPECT_EQ (observation.itemName, "Mouse 8 recorder");
    EXPECT_FALSE (observation.invokePatternAvailable);
    EXPECT_TRUE (observation.directChildrenAreOnlyProcessorItems);

    window.setVisible (false);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}

TEST_F (ProcessorInventoryWindowsUIAutomationTests, ExternalClientSeesEveryLoadedProcessorAcrossTabsExactlyOnce)
{
    SmokeTestProcessor firstRootProbe;
    SmokeTestProcessor firstRootRecorder;
    SmokeTestProcessor eighthRootProbe;
    SmokeTestProcessor eighthRootRecorder;
    firstRootProbe.setNodeId (4101);
    firstRootRecorder.setNodeId (4102);
    eighthRootProbe.setNodeId (4801);
    eighthRootRecorder.setNodeId (4802);

    GenericEditor firstRootProbeEditor (&firstRootProbe);
    GenericEditor firstRootRecorderEditor (&firstRootRecorder);
    GenericEditor eighthRootProbeEditor (&eighthRootProbe);
    GenericEditor eighthRootRecorderEditor (&eighthRootRecorder);
    firstRootProbeEditor.setDisplayName ("Probe 1");
    firstRootRecorderEditor.setDisplayName ("Probe 1 recorder");
    eighthRootProbeEditor.setDisplayName ("Probe 8");
    eighthRootRecorderEditor.setDisplayName ("Probe 8 recorder");
    firstRootProbeEditor.setDescription ("Predecessor node ID: none.");
    firstRootRecorderEditor.setDescription ("Predecessor node ID: 4101.");
    eighthRootProbeEditor.setDescription ("Predecessor node ID: none.");
    eighthRootRecorderEditor.setDescription ("Predecessor node ID: 4801.");

    TextButton firstRootEditorDescendant ("Root 1 editor action");
    firstRootEditorDescendant.setComponentID ("oe.test.root1.editor.descendant");
    firstRootRecorderEditor.addAndMakeVisible (firstRootEditorDescendant);
    firstRootEditorDescendant.setBounds (0, 0, 120, 24);
    TextButton eighthRootEditorDescendant ("Root 8 editor action");
    eighthRootEditorDescendant.setComponentID ("oe.test.root8.editor.descendant");
    eighthRootRecorderEditor.addAndMakeVisible (eighthRootEditorDescendant);
    eighthRootEditorDescendant.setBounds (0, 0, 120, 24);

    auto tabs = std::make_unique<SignalChainTabComponent>();
    auto* viewport = new EditorViewport (tabs.get());
    DocumentWindow window ("Multi-root processor inventory UIA", Colours::black, 0);
    window.setContentOwned (tabs.release(), true);
    window.centreWithSize (700, 300);
    window.setVisible (true);
    ASSERT_NE (window.getPeer(), nullptr);
    MessageManager::getInstance()->runDispatchLoopUntil (100);

    std::vector<ProcessorAccessibilitySnapshotItem> snapshot;
    StringArray expectedIds;
    StringArray expectedNames;
    StringArray expectedPredecessors;
    for (int root = 1; root <= 8; ++root)
    {
        const auto probeId = 4000 + root * 100 + 1;
        const auto recorderId = probeId + 1;
        snapshot.push_back ({ probeId, "Probe " + String (root), std::nullopt });
        snapshot.push_back ({ recorderId, "Probe " + String (root) + " recorder", probeId });
        expectedIds.add ("oe.processor." + String (probeId));
        expectedIds.add ("oe.processor." + String (recorderId));
        expectedNames.add ("Probe " + String (root));
        expectedNames.add ("Probe " + String (root) + " recorder");
        expectedPredecessors.add ("Predecessor node ID: none.");
        expectedPredecessors.add ("Predecessor node ID: " + String (probeId) + ".");
    }
    viewport->updateAccessibleProcessorInventory (snapshot);

    viewport->updateVisibleEditors (
        Array<GenericEditor*> { &eighthRootProbeEditor, &eighthRootRecorderEditor }, 8, 7);
    MessageManager::getInstance()->runDispatchLoopUntil (100);

    const auto assertInventory = [&] (const UiaObservation& observation,
                                      int visibleRoot)
    {
        ASSERT_TRUE (SUCCEEDED (observation.result)) << describeObservation (observation);
        ASSERT_TRUE (observation.rootFound) << describeObservation (observation);
        EXPECT_TRUE (observation.directChildrenAreOnlyProcessorItems);
        EXPECT_EQ (observation.directChildCount, 16) << describeObservation (observation);
        EXPECT_EQ (observation.directProcessorAutomationIds.joinIntoString (","),
                   expectedIds.joinIntoString (","));
        ASSERT_EQ (observation.directProcessorItems.size(), 16u)
            << describeObservation (observation);
        for (size_t index = 0; index < observation.directProcessorItems.size(); ++index)
        {
            const auto& item = observation.directProcessorItems[index];
            EXPECT_EQ (item.name, expectedNames[static_cast<int> (index)]);
            EXPECT_TRUE (item.fullDescription.contains (
                expectedPredecessors[static_cast<int> (index)]));
            EXPECT_EQ (item.isOffscreen, static_cast<int> (index / 2) + 1 != visibleRoot);
            EXPECT_FALSE (item.invokePatternAvailable);
        }
    };

    const auto eighthRootObservation = observeWhilePumpingMessages (
        static_cast<HWND> (window.getPeer()->getNativeHandle()));
    assertInventory (eighthRootObservation, 8);
    EXPECT_FALSE (eighthRootObservation.firstRootEditorDescendantFound);
    EXPECT_TRUE (eighthRootObservation.eighthRootEditorDescendantFound);

    snapshot.front().name = "Probe 1 renamed";
    expectedNames.set (0, "Probe 1 renamed");
    viewport->updateAccessibleProcessorInventory (snapshot);
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    const auto renamedHiddenObservation = observeWhilePumpingMessages (
        static_cast<HWND> (window.getPeer()->getNativeHandle()));
    assertInventory (renamedHiddenObservation, 8);
    EXPECT_EQ (renamedHiddenObservation.directProcessorAutomationIds[0], "oe.processor.4101");
    EXPECT_EQ (renamedHiddenObservation.directProcessorItems[0].name, "Probe 1 renamed");

    firstRootProbeEditor.setDisplayName ("Probe 1 renamed");
    viewport->updateVisibleEditors (
        Array<GenericEditor*> { &firstRootProbeEditor, &firstRootRecorderEditor }, 8, 0);
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    const auto firstRootObservation = observeWhilePumpingMessages (
        static_cast<HWND> (window.getPeer()->getNativeHandle()));
    assertInventory (firstRootObservation, 1);
    EXPECT_TRUE (firstRootObservation.firstRootEditorDescendantFound);
    EXPECT_FALSE (firstRootObservation.eighthRootEditorDescendantFound);

    window.setVisible (false);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}

TEST_F (ProcessorInventoryWindowsUIAutomationTests,
        ExternalClientSeesRefreshedPredecessorOnVisibleGenericEditor)
{
    SmokeTestProcessor processor;
    processor.setNodeId (4242);
    GenericEditor editor (&processor);
    editor.setDisplayName ("Mouse 8 recorder");
    // Seed richer description before inventory wiring appends/replaces the clause.
    ASSERT_TRUE (editor.getDescription().contains ("Constructor-time processor name:"));
    ASSERT_FALSE (editor.getDescription().contains ("Predecessor node ID:"));

    auto tabs = std::make_unique<SignalChainTabComponent>();
    auto* viewport = new EditorViewport (tabs.get());
    DocumentWindow window ("Mutable predecessor UIA", Colours::black, 0);
    window.setContentOwned (tabs.release(), true);
    window.centreWithSize (700, 300);
    window.setVisible (true);
    ASSERT_NE (window.getPeer(), nullptr);
    MessageManager::getInstance()->runDispatchLoopUntil (100);

    viewport->updateVisibleEditors (Array<GenericEditor*> { &editor }, 1, 0);
    viewport->updateAccessibleProcessorInventory (
        { { 4242, "Mouse 8 recorder", 11 } });
    MessageManager::getInstance()->runDispatchLoopUntil (100);

    const auto first = observeWhilePumpingMessages (
        static_cast<HWND> (window.getPeer()->getNativeHandle()));
    ASSERT_TRUE (SUCCEEDED (first.result)) << describeObservation (first);
    ASSERT_TRUE (first.itemFound) << describeObservation (first);
    ASSERT_EQ (first.directProcessorItems.size(), 1u) << describeObservation (first);
    EXPECT_EQ (first.directProcessorItems[0].name, "Mouse 8 recorder");
    EXPECT_TRUE (first.directProcessorItems[0].fullDescription.contains (
        "Constructor-time processor name:"));
    EXPECT_TRUE (first.directProcessorItems[0].fullDescription.contains (
        "Predecessor node ID: 11."));

    viewport->updateAccessibleProcessorInventory (
        { { 4242, "Mouse 8 recorder", 99 } });
    MessageManager::getInstance()->runDispatchLoopUntil (100);

    const auto second = observeWhilePumpingMessages (
        static_cast<HWND> (window.getPeer()->getNativeHandle()));
    ASSERT_TRUE (SUCCEEDED (second.result)) << describeObservation (second);
    ASSERT_TRUE (second.itemFound) << describeObservation (second);
    ASSERT_EQ (second.directProcessorItems.size(), 1u) << describeObservation (second);
    EXPECT_EQ (second.directProcessorItems[0].name, "Mouse 8 recorder");
    EXPECT_TRUE (second.directProcessorItems[0].fullDescription.contains (
        "Constructor-time processor name:"));
    EXPECT_TRUE (second.directProcessorItems[0].fullDescription.contains (
        "Predecessor node ID: 99."));
    EXPECT_FALSE (second.directProcessorItems[0].fullDescription.contains (
        "Predecessor node ID: 11."));
    EXPECT_TRUE (editor.getDescription().contains (
        "Constructor-time processor name:"));
    EXPECT_TRUE (editor.getDescription().contains ("Predecessor node ID: 99."));
    EXPECT_FALSE (editor.getDescription().contains ("Predecessor node ID: 11."));

    window.setVisible (false);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}
