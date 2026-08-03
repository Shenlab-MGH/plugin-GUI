#include "../../Source/MainWindow.h"
#include "gtest/gtest.h"

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#endif

namespace
{
class AccessibilityTestApplication final : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Open Ephys UIA Test"; }
    const String getApplicationVersion() override { return "1.1.0"; }
    void initialise (const String&) override {}
    void shutdown() override {}

    bool initialiseForTest() { return JUCEApplicationBase::initialiseApp(); }
    void shutdownForTest() { JUCEApplicationBase::shutdownApp(); }
};

JUCEApplicationBase* createAccessibilityTestApplication()
{
    return new AccessibilityTestApplication();
}

class MainDocumentWindowAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        JUCEApplicationBase::createInstance = createAccessibilityTestApplication;
        application = std::make_unique<AccessibilityTestApplication>();
        MessageManager::getInstance();
        messageManagerLock = std::make_unique<MessageManagerLock>();
        ASSERT_TRUE (application->initialiseForTest());
    }

    void TearDown() override
    {
        application->shutdownForTest();
        messageManagerLock.reset();
        application.reset();
        JUCEApplicationBase::createInstance = nullptr;
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    std::unique_ptr<AccessibilityTestApplication> application;
    std::unique_ptr<MessageManagerLock> messageManagerLock;
};

#if JUCE_WINDOWS
struct UiaObservation
{
    bool found = false;
    HRESULT result = E_FAIL;
    String automationId;
    String discoveredElements;
};

UiaObservation findElementByProperty (HWND windowHandle,
                                      PROPERTYID property,
                                      const wchar_t* expectedValue)
{
    UiaObservation observation;
    const auto comResult = CoInitializeEx (nullptr, COINIT_MULTITHREADED);

    if (FAILED (comResult))
    {
        observation.result = comResult;
        return observation;
    }

    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    observation.result = CoCreateInstance (CLSID_CUIAutomation,
                                           nullptr,
                                           CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS (automation.GetAddressOf()));

    Microsoft::WRL::ComPtr<IUIAutomationElement> root;

    if (SUCCEEDED (observation.result))
        observation.result = automation->ElementFromHandle (windowHandle, root.GetAddressOf());

    VARIANT expected;
    VariantInit (&expected);
    expected.vt = VT_BSTR;
    expected.bstrVal = SysAllocString (expectedValue);

    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;

    if (SUCCEEDED (observation.result))
        observation.result = automation->CreatePropertyCondition (property, expected, condition.GetAddressOf());

    VariantClear (&expected);

    const auto deadline = Time::getMillisecondCounter() + 2000;

    while (SUCCEEDED (observation.result)
           && Time::getMillisecondCounter() < deadline)
    {
        Microsoft::WRL::ComPtr<IUIAutomationElement> element;
        observation.result = root->FindFirst (TreeScope_Descendants,
                                              condition.Get(),
                                              element.GetAddressOf());

        if (SUCCEEDED (observation.result) && element != nullptr)
        {
            BSTR automationId = nullptr;
            observation.result = element->get_CurrentAutomationId (&automationId);

            if (SUCCEEDED (observation.result))
            {
                observation.found = true;
                observation.automationId = String (automationId);
            }

            SysFreeString (automationId);
            break;
        }

        Thread::sleep (10);
    }

    if (! observation.found && SUCCEEDED (observation.result))
    {
        Microsoft::WRL::ComPtr<IUIAutomationCondition> trueCondition;
        Microsoft::WRL::ComPtr<IUIAutomationElementArray> elements;
        observation.result = automation->CreateTrueCondition (trueCondition.GetAddressOf());

        if (SUCCEEDED (observation.result))
            observation.result = root->FindAll (TreeScope_Descendants,
                                                trueCondition.Get(),
                                                elements.GetAddressOf());

        int count = 0;

        if (SUCCEEDED (observation.result) && elements != nullptr)
            observation.result = elements->get_Length (&count);

        for (int index = 0; SUCCEEDED (observation.result) && index < count; ++index)
        {
            Microsoft::WRL::ComPtr<IUIAutomationElement> element;
            observation.result = elements->GetElement (index, element.GetAddressOf());
            BSTR name = nullptr;
            BSTR automationId = nullptr;

            if (SUCCEEDED (observation.result))
                observation.result = element->get_CurrentName (&name);
            if (SUCCEEDED (observation.result))
                observation.result = element->get_CurrentAutomationId (&automationId);

            if (SUCCEEDED (observation.result))
                observation.discoveredElements += String (name) + "|" + String (automationId) + ";";

            SysFreeString (name);
            SysFreeString (automationId);
        }
    }

    CoUninitialize();
    return observation;
}

UiaObservation observeWhilePumpingMessages (HWND windowHandle,
                                            PROPERTYID property,
                                            const wchar_t* expectedValue)
{
    UiaObservation observation;
    std::atomic<bool> finished { false };

    std::thread worker ([&]
                        {
                            observation = findElementByProperty (windowHandle,
                                                                 property,
                                                                 expectedValue);
                            finished.store (true);
                        });

    const auto deadline = Time::getMillisecondCounter() + 3000;

    while (! finished.load() && Time::getMillisecondCounter() < deadline)
        MessageManager::getInstance()->runDispatchLoopUntil (10);

    worker.join();
    return observation;
}

void showTestWindow (MainDocumentWindow& window,
                     Component& content)
{
    window.setContentNonOwned (&content, false);
    window.centreWithSize (360, 120);
    window.addToDesktop();
    window.setVisible (true);
    window.toFront (true);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}
#endif
} // namespace

TEST_F (MainDocumentWindowAccessibilityTests, ExposesTheWindowAndItsContentToAccessibilityClients)
{
    MainDocumentWindow window;
    TextButton contentButton ("Accessible child");

    window.setContentNonOwned (&contentButton, false);

    EXPECT_TRUE (window.isAccessible());
    EXPECT_TRUE (contentButton.isAccessible());
}

#if JUCE_WINDOWS
TEST_F (MainDocumentWindowAccessibilityTests, UsesComponentIdAsTheWindowsAutomationId)
{
    MainDocumentWindow window;
    Component content;
    TextButton identifiedButton ("Identified child");
    identifiedButton.setComponentID ("oe.test.identified_child");
    identifiedButton.setBounds (10, 10, 160, 30);
    content.addAndMakeVisible (identifiedButton);
    showTestWindow (window, content);

    const auto observation = observeWhilePumpingMessages (
        static_cast<HWND> (window.getWindowHandle()),
        UIA_AutomationIdPropertyId,
        L"oe.test.identified_child");

    ASSERT_TRUE (SUCCEEDED (observation.result));
    ASSERT_TRUE (observation.found) << observation.discoveredElements;
    EXPECT_EQ (observation.automationId, "oe.test.identified_child");
}

TEST_F (MainDocumentWindowAccessibilityTests, PreservesTheWindowsAutomationIdFallbackWithoutAComponentId)
{
    MainDocumentWindow window;
    Component content;
    TextButton fallbackButton ("Fallback child");
    fallbackButton.setBounds (10, 10, 160, 30);
    content.addAndMakeVisible (fallbackButton);
    showTestWindow (window, content);

    const auto observation = observeWhilePumpingMessages (
        static_cast<HWND> (window.getWindowHandle()),
        UIA_NamePropertyId,
        L"Fallback child");

    ASSERT_TRUE (SUCCEEDED (observation.result));
    ASSERT_TRUE (observation.found) << observation.discoveredElements;
    EXPECT_TRUE (observation.automationId.startsWith ("Fallback child"));
}
#endif
