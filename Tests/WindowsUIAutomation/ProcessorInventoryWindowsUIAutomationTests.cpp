#include "../../Source/UI/EditorViewport.h"
#include "gtest/gtest.h"

#include <UIAutomation.h>
#include <combaseapi.h>
#include <oleauto.h>

#include <atomic>
#include <thread>

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

struct UiaObservation
{
    HRESULT result = E_FAIL;
    CONTROLTYPEID rootType = 0;
    CONTROLTYPEID itemType = 0;
    String itemName;
    bool rootFound = false;
    bool itemFound = false;
    bool invokePatternAvailable = false;
    String discoveredElements;
};

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
    ComOwner<IUIAutomationElementArray> descendants;
    if (SUCCEEDED (automation->CreateTrueCondition (trueCondition.put()))
        && SUCCEEDED (root->FindAll (TreeScope_Descendants, trueCondition.get(), descendants.put()))
        && descendants.get() != nullptr)
    {
        int count = 0;
        descendants->get_Length (&count);
        for (int index = 0; index < count; ++index)
        {
            ComOwner<IUIAutomationElement> element;
            if (FAILED (descendants->GetElement (index, element.put())))
                continue;
            BSTR id = nullptr;
            BSTR name = nullptr;
            element->get_CurrentAutomationId (&id);
            element->get_CurrentName (&name);
            observation.discoveredElements += String (id) + "|" + String (name) + ";";
            SysFreeString (id);
            SysFreeString (name);
        }
    }

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
    void SetUp() override
    {
        MessageManager::deleteInstance();
        MessageManager::getInstance();
    }

    void TearDown() override
    {
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
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
    ASSERT_TRUE (SUCCEEDED (observation.result)) << std::hex << observation.result;
    EXPECT_TRUE (observation.rootFound);
    EXPECT_TRUE (observation.itemFound) << observation.discoveredElements;
    EXPECT_EQ (observation.rootType, UIA_ListControlTypeId);
    EXPECT_EQ (observation.itemType, UIA_ListItemControlTypeId);
    EXPECT_EQ (observation.itemName, "Mouse 8 recorder");
    EXPECT_FALSE (observation.invokePatternAvailable);

    window.setVisible (false);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}
