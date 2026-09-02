#include "../../Source/MainWindow.h"
#include "../../Source/UI/ControlPanel.h"
#include "gtest/gtest.h"

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#endif

namespace
{
class ControlPanelAccessibilityTestApplication final : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Open Ephys UIA Test"; }
    const String getApplicationVersion() override { return "1.1.0"; }
    void initialise (const String&) override {}
    void shutdown() override {}

    bool initialiseForTest() { return JUCEApplicationBase::initialiseApp(); }
    void shutdownForTest() { JUCEApplicationBase::shutdownApp(); }
};

JUCEApplicationBase* createControlPanelAccessibilityTestApplication()
{
    return new ControlPanelAccessibilityTestApplication();
}

class ControlPanelAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        JUCEApplicationBase::createInstance = createControlPanelAccessibilityTestApplication;
        application = std::make_unique<ControlPanelAccessibilityTestApplication>();
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

    std::unique_ptr<ControlPanelAccessibilityTestApplication> application;
    std::unique_ptr<MessageManagerLock> messageManagerLock;
};

class TestClock : public Clock
{
public:
    using Clock::createAccessibilityHandler;
};

void expectSemanticButton (Button& button,
                           const String& expectedId,
                           const String& expectedTitle,
                           const String& expectedDescription)
{
#if JUCE_WINDOWS
    EXPECT_EQ (button.getComponentID(), expectedId);
    EXPECT_EQ (button.getTitle(), expectedTitle);
    EXPECT_EQ (button.getDescription(), expectedDescription);
    EXPECT_TRUE (button.isAccessible());
#else
    juce::ignoreUnused (button, expectedId, expectedTitle, expectedDescription);
    EXPECT_TRUE (button.getComponentID().isEmpty());
#endif
}

#if JUCE_WINDOWS
constexpr auto uiaWorkerTimeoutMilliseconds = 10000;

Component* findDescendantByAutomationId (Component& root, const String& automationId)
{
    for (int index = 0; index < root.getNumChildComponents(); ++index)
    {
        auto* child = root.getChildComponent (index);

        if (child->getComponentID() == automationId)
            return child;

        if (auto* descendant = findDescendantByAutomationId (*child, automationId))
            return descendant;
    }

    return nullptr;
}

struct UiaButtonObservation
{
    HRESULT result = E_FAIL;
    bool found = false;
    bool hasInvokePattern = false;
    bool hasTogglePattern = false;
    String automationId;
    String name;
    String fullDescription;
    String helpText;
};

enum class UiaButtonAction
{
    invoke,
    toggle
};

struct UiaButtonActionObservation
{
    HRESULT result = E_FAIL;
    ToggleState toggleState = ToggleState_Indeterminate;
};

class ButtonClickListener final : public Button::Listener
{
public:
    void buttonClicked (Button* button) override
    {
        ++clickCount;
        stateAtClick.store (button->getToggleState());
    }

    std::atomic<int> clickCount { 0 };
    std::atomic<bool> stateAtClick { false };
};

[[noreturn]] void abortTimedOutUiaWorker (const char* operation)
{
    ADD_FAILURE() << "Timed out waiting for the Windows UI Automation " << operation
                  << " worker; aborting the isolated UI test process.";
    std::fflush (nullptr);
    std::abort();
}

String readStringProperty (IUIAutomationElement& element, PROPERTYID property, HRESULT& result)
{
    VARIANT value;
    VariantInit (&value);
    result = element.GetCurrentPropertyValue (property, &value);

    String text;

    if (SUCCEEDED (result) && value.vt == VT_BSTR)
        text = String (value.bstrVal);

    VariantClear (&value);
    return text;
}

UiaButtonObservation observeButton (HWND windowHandle, const wchar_t* automationId)
{
    UiaButtonObservation observation;
    const auto comResult = CoInitializeEx (nullptr, COINIT_MULTITHREADED);

    if (FAILED (comResult))
    {
        observation.result = comResult;
        return observation;
    }

    const ScopeGuard uninitialiseCom { [] { CoUninitialize(); } };

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
    expected.bstrVal = SysAllocString (automationId);
    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;

    if (SUCCEEDED (observation.result))
        observation.result = automation->CreatePropertyCondition (UIA_AutomationIdPropertyId,
                                                                  expected,
                                                                  condition.GetAddressOf());

    VariantClear (&expected);
    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    const auto deadline = Time::getMillisecondCounter() + 2000;

    while (SUCCEEDED (observation.result)
           && element == nullptr
           && Time::getMillisecondCounter() < deadline)
    {
        observation.result = root->FindFirst (TreeScope_Descendants,
                                              condition.Get(),
                                              element.GetAddressOf());

        if (element == nullptr)
            Thread::sleep (10);
    }

    if (SUCCEEDED (observation.result) && element != nullptr)
    {
        observation.found = true;
        observation.automationId = readStringProperty (*element.Get(),
                                                       UIA_AutomationIdPropertyId,
                                                       observation.result);

        if (SUCCEEDED (observation.result))
            observation.name = readStringProperty (*element.Get(), UIA_NamePropertyId, observation.result);
        if (SUCCEEDED (observation.result))
            observation.fullDescription = readStringProperty (*element.Get(),
                                                               UIA_FullDescriptionPropertyId,
                                                               observation.result);
        if (SUCCEEDED (observation.result))
            observation.helpText = readStringProperty (*element.Get(),
                                                       UIA_HelpTextPropertyId,
                                                       observation.result);

        Microsoft::WRL::ComPtr<IUnknown> invokePattern;
        Microsoft::WRL::ComPtr<IUnknown> togglePattern;

        if (SUCCEEDED (observation.result))
        {
            observation.result = element->GetCurrentPattern (UIA_InvokePatternId,
                                                             invokePattern.GetAddressOf());
            observation.hasInvokePattern = invokePattern != nullptr;
        }

        if (SUCCEEDED (observation.result))
        {
            observation.result = element->GetCurrentPattern (UIA_TogglePatternId,
                                                             togglePattern.GetAddressOf());
            observation.hasTogglePattern = togglePattern != nullptr;
        }
    }

    return observation;
}

UiaButtonObservation observeWhilePumpingMessages (HWND windowHandle,
                                                   const wchar_t* automationId)
{
    UiaButtonObservation observation;
    std::atomic<bool> finished { false };

    std::thread worker ([&]
                        {
                            observation = observeButton (windowHandle, automationId);
                            finished.store (true);
                        });

    const auto deadline = Time::getMillisecondCounter() + uiaWorkerTimeoutMilliseconds;

    while (! finished.load())
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (Time::getMillisecondCounter() >= deadline)
            abortTimedOutUiaWorker ("observation");
    }

    worker.join();
    return observation;
}

UiaButtonActionObservation performButtonAction (HWND windowHandle,
                                                const wchar_t* automationId,
                                                UiaButtonAction action)
{
    UiaButtonActionObservation observation;
    const auto comResult = CoInitializeEx (nullptr, COINIT_MULTITHREADED);

    if (FAILED (comResult))
    {
        observation.result = comResult;
        return observation;
    }

    const ScopeGuard uninitialiseCom { [] { CoUninitialize(); } };

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
    expected.bstrVal = SysAllocString (automationId);
    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;

    if (SUCCEEDED (observation.result))
        observation.result = automation->CreatePropertyCondition (UIA_AutomationIdPropertyId,
                                                                  expected,
                                                                  condition.GetAddressOf());

    VariantClear (&expected);
    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    const auto findDeadline = Time::getMillisecondCounter() + 2000;

    while (SUCCEEDED (observation.result)
           && element == nullptr
           && Time::getMillisecondCounter() < findDeadline)
    {
        observation.result = root->FindFirst (TreeScope_Descendants,
                                              condition.Get(),
                                              element.GetAddressOf());

        if (element == nullptr)
            Thread::sleep (10);
    }

    if (SUCCEEDED (observation.result) && element == nullptr)
        observation.result = UIA_E_ELEMENTNOTAVAILABLE;

    Microsoft::WRL::ComPtr<IUnknown> actionPattern;

    if (SUCCEEDED (observation.result) && element != nullptr)
    {
        const auto patternId = action == UiaButtonAction::invoke ? UIA_InvokePatternId
                                                                 : UIA_TogglePatternId;
        observation.result = element->GetCurrentPattern (patternId, actionPattern.GetAddressOf());
    }

    if (SUCCEEDED (observation.result) && actionPattern == nullptr)
        observation.result = UIA_E_NOTSUPPORTED;

    if (SUCCEEDED (observation.result) && actionPattern != nullptr)
    {
        if (action == UiaButtonAction::invoke)
        {
            Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invokePattern;
            observation.result = actionPattern.As (&invokePattern);

            if (SUCCEEDED (observation.result))
                observation.result = invokePattern->Invoke();
        }
        else
        {
            Microsoft::WRL::ComPtr<IUIAutomationTogglePattern> togglePattern;
            observation.result = actionPattern.As (&togglePattern);

            if (SUCCEEDED (observation.result))
                observation.result = togglePattern->Toggle();
        }
    }

    Microsoft::WRL::ComPtr<IUnknown> readbackPattern;
    Microsoft::WRL::ComPtr<IUIAutomationTogglePattern> togglePattern;

    if (SUCCEEDED (observation.result) && element != nullptr)
        observation.result = element->GetCurrentPattern (UIA_TogglePatternId,
                                                         readbackPattern.GetAddressOf());

    if (SUCCEEDED (observation.result) && readbackPattern == nullptr)
        observation.result = UIA_E_NOTSUPPORTED;

    if (SUCCEEDED (observation.result))
        observation.result = readbackPattern.As (&togglePattern);

    const auto readbackDeadline = Time::getMillisecondCounter() + 2000;

    while (SUCCEEDED (observation.result)
           && observation.toggleState != ToggleState_On
           && Time::getMillisecondCounter() < readbackDeadline)
    {
        observation.result = togglePattern->get_CurrentToggleState (&observation.toggleState);

        if (observation.toggleState != ToggleState_On)
            Thread::sleep (10);
    }

    return observation;
}

UiaButtonActionObservation performActionWhilePumpingMessages (HWND windowHandle,
                                                              const wchar_t* automationId,
                                                              UiaButtonAction action)
{
    UiaButtonActionObservation observation;
    std::atomic<bool> finished { false };

    std::thread worker ([&]
                        {
                            observation = performButtonAction (windowHandle, automationId, action);
                            finished.store (true);
                        });

    const auto deadline = Time::getMillisecondCounter() + uiaWorkerTimeoutMilliseconds;

    while (! finished.load())
    {
        MessageManager::getInstance()->runDispatchLoopUntil (10);

        if (Time::getMillisecondCounter() >= deadline)
            abortTimedOutUiaWorker ("action");
    }

    worker.join();
    return observation;
}

void expectButtonSemantics (const UiaButtonObservation& observation,
                            const String& expectedId,
                            const String& expectedName,
                            const String& expectedDescription,
                            const String& expectedHelp)
{
    ASSERT_TRUE (SUCCEEDED (observation.result));
    ASSERT_TRUE (observation.found);
    EXPECT_EQ (observation.automationId, expectedId);
    EXPECT_EQ (observation.name, expectedName);
    EXPECT_EQ (observation.fullDescription, expectedDescription);
    EXPECT_EQ (observation.helpText, expectedHelp);
    EXPECT_TRUE (observation.hasInvokePattern);
    EXPECT_TRUE (observation.hasTogglePattern);
}
#endif
} // namespace

TEST_F (ControlPanelAccessibilityTests, ExposesGlobalActionButtons)
{
    PlayButton acquisition;
    expectSemanticButton (acquisition,
                          "oe.control.acquisition",
                          "Acquisition",
                          "Start or stop data acquisition.");

    RecordButton recording;
    expectSemanticButton (recording,
                          "oe.control.recording",
                          "Recording",
                          "Start or stop writing data to disk.");

    FilenameEditorButton filename;
    expectSemanticButton (filename,
                          "oe.control.recording.filename",
                          "Recording filename",
                          "Edit the recording filename.");

    NewDirectoryButton newDirectory;
    expectSemanticButton (newDirectory,
                          "oe.control.recording.new_directory",
                          "New recording directory",
                          "Start a new data directory for the next recording.");

    ForceNewDirectoryButton forceNewDirectory;
    expectSemanticButton (forceNewDirectory,
                          "oe.control.recording.force_new_directory",
                          "Force new recording directories",
                          "Force a new data directory for each recording.");

    RecordingOptionsButton recordingOptions;
    expectSemanticButton (recordingOptions,
                          "oe.control.recording.options",
                          "Recording options",
                          "Show or hide recording options.");
    EXPECT_TRUE (recordingOptions.getClickingTogglesState());
}

TEST_F (ControlPanelAccessibilityTests, ExposesHealthMetersAsReadOnlyRanges)
{
    CPUMeter cpu;
    cpu.updateCPU (0.42f);

#if JUCE_WINDOWS
    EXPECT_EQ (cpu.getComponentID(), "oe.status.cpu_usage");
    EXPECT_EQ (cpu.getTitle(), "CPU usage");
#endif
    auto cpuHandler = cpu.createAccessibilityHandler();
    ASSERT_NE (cpuHandler, nullptr);
    EXPECT_EQ (cpuHandler->getRole(), AccessibilityRole::progressBar);
    auto* cpuValue = cpuHandler->getValueInterface();
    ASSERT_NE (cpuValue, nullptr);
    EXPECT_TRUE (cpuValue->isReadOnly());
    EXPECT_NEAR (cpuValue->getCurrentValue(), 0.42, 0.001);
    EXPECT_TRUE (cpuValue->getRange().isValid());
    EXPECT_DOUBLE_EQ (cpuValue->getRange().getMinimumValue(), 0.0);
    EXPECT_DOUBLE_EQ (cpuValue->getRange().getMaximumValue(), 1.0);

    DiskSpaceMeter disk;
    disk.updateDiskSpace (0.65f);

#if JUCE_WINDOWS
    EXPECT_EQ (disk.getComponentID(), "oe.status.disk_usage");
    EXPECT_EQ (disk.getTitle(), "Disk usage");
    EXPECT_EQ (disk.getDescription(), "Fraction of recording-volume space currently used.");
#endif
    auto diskHandler = disk.createAccessibilityHandler();
    ASSERT_NE (diskHandler, nullptr);
    EXPECT_EQ (diskHandler->getRole(), AccessibilityRole::progressBar);
    auto* diskValue = diskHandler->getValueInterface();
    ASSERT_NE (diskValue, nullptr);
    EXPECT_TRUE (diskValue->isReadOnly());
    EXPECT_NEAR (diskValue->getCurrentValue(), 0.65, 0.001);
    EXPECT_TRUE (diskValue->getRange().isValid());
    EXPECT_DOUBLE_EQ (diskValue->getRange().getMinimumValue(), 0.0);
    EXPECT_DOUBLE_EQ (diskValue->getRange().getMaximumValue(), 1.0);
}

TEST_F (ControlPanelAccessibilityTests, ExposesClockAsReadOnlyFormattedText)
{
    TestClock clock;

#if JUCE_WINDOWS
    EXPECT_EQ (clock.getComponentID(), "oe.status.elapsed_time");
    EXPECT_EQ (clock.getTitle(), "Elapsed time");
#endif

    auto handler = clock.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (handler->getRole(), AccessibilityRole::staticText);

    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    EXPECT_TRUE (value->isReadOnly());
    EXPECT_EQ (value->getCurrentValueAsString(), "0 min 0 s");

    clock.setMode (Clock::HHMMSS);
    EXPECT_EQ (value->getCurrentValueAsString(), "00:00:00");
}

TEST_F (ControlPanelAccessibilityTests, UsesClockStatusForItsAccessibleValue)
{
    TestClock clock;
    const auto state = clock.getStatus();
    auto handler = clock.createAccessibilityHandler();
    ASSERT_NE (handler->getValueInterface(), nullptr);
    EXPECT_EQ (state.display, handler->getValueInterface()->getCurrentValueAsString());
    EXPECT_EQ (state.elapsedMilliseconds, 0);
    EXPECT_FALSE (state.running);
    EXPECT_FALSE (state.recording);
}

TEST_F (ControlPanelAccessibilityTests, SharesForceNewDirectoryStateWithTheButton)
{
    ControlPanel panel (nullptr, nullptr, true);
    panel.setForceNewDirectory (true);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().forceNewDirectory);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().newDirectoryRequested);

    panel.setNewDirectoryRequested (false);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().newDirectoryRequested);

    panel.setForceNewDirectory (false);
    EXPECT_FALSE (panel.getRecordingOptionsStatus().forceNewDirectory);

    panel.setNewDirectoryRequested (false);
    EXPECT_FALSE (panel.getRecordingOptionsStatus().newDirectoryRequested);

    panel.setRecordingOptionsExpanded (true);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().expanded);
}

#if JUCE_WINDOWS
TEST_F (ControlPanelAccessibilityTests, ExposesAcquisitionAndRecordingButtonsThroughWindowsUia)
{
    MainDocumentWindow window;
    Component content;
    PlayButton acquisition;
    RecordButton recording;

    acquisition.setBounds (10, 10, 40, 40);
    recording.setBounds (60, 10, 40, 40);
    content.addAndMakeVisible (acquisition);
    content.addAndMakeVisible (recording);
    window.setContentNonOwned (&content, false);
    window.centreWithSize (160, 80);
    window.addToDesktop();
    window.setVisible (true);
    window.toFront (true);
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    const auto windowHandle = static_cast<HWND> (window.getWindowHandle());
    expectButtonSemantics (observeWhilePumpingMessages (windowHandle, L"oe.control.acquisition"),
                           "oe.control.acquisition",
                           "Acquisition",
                           "Start or stop data acquisition.",
                           "Start/stop acquisition");
    expectButtonSemantics (observeWhilePumpingMessages (windowHandle, L"oe.control.recording"),
                           "oe.control.recording",
                           "Recording",
                           "Start or stop writing data to disk.",
                           "Start/stop writing to disk");

    window.setVisible (false);
    window.removeFromDesktop();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}

TEST_F (ControlPanelAccessibilityTests, ExposesRecordingDirectoryThroughWindowsUia)
{
    MainDocumentWindow window;
    Component content;
    ControlPanel panel (nullptr, nullptr, true);

    auto* directory = findDescendantByAutomationId (panel, "oe.control.recording.directory");
    ASSERT_NE (directory, nullptr);
    EXPECT_EQ (directory->getTitle(), "Recording directory");
    EXPECT_EQ (directory->getDescription(), "Read or edit the recording parent directory.");
    EXPECT_TRUE (directory->isAccessible());

    // Exercise the real FilenameComponent configured by ControlPanel, but keep
    // the native UIA query scoped to that component. Resizing the console-mode
    // ControlPanel unnecessarily lays out controls that were never attached to
    // its component tree, making this contract depend on unrelated lifetimes.
    content.setBounds (0, 0, 900, 100);
    content.addAndMakeVisible (directory);
    directory->setBounds (0, 0, 390, 32);
    window.setContentNonOwned (&content, false);
    window.centreWithSize (900, 120);
    window.addToDesktop();
    window.setVisible (true);
    window.toFront (true);
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    const auto observation = observeWhilePumpingMessages (
        static_cast<HWND> (window.getWindowHandle()),
        L"oe.control.recording.directory");
    ASSERT_TRUE (SUCCEEDED (observation.result));
    ASSERT_TRUE (observation.found);
    EXPECT_EQ (observation.automationId, "oe.control.recording.directory");
    EXPECT_EQ (observation.name, "Recording directory");
    EXPECT_EQ (observation.fullDescription, "Read or edit the recording parent directory.");

    window.setVisible (false);
    window.removeFromDesktop();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}

TEST_F (ControlPanelAccessibilityTests, RoutesWindowsUiaActionsToButtonListenersAndToggleState)
{
    MainDocumentWindow window;
    Component content;
    PlayButton acquisition;
    RecordButton recording;
    ButtonClickListener acquisitionListener;
    ButtonClickListener recordingListener;

    acquisition.addListener (&acquisitionListener);
    recording.addListener (&recordingListener);
    acquisition.setBounds (10, 10, 40, 40);
    recording.setBounds (60, 10, 40, 40);
    content.addAndMakeVisible (acquisition);
    content.addAndMakeVisible (recording);
    window.setContentNonOwned (&content, false);
    window.centreWithSize (160, 80);
    window.addToDesktop();
    window.setVisible (true);
    window.toFront (true);
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    const auto windowHandle = static_cast<HWND> (window.getWindowHandle());
    const auto invokeObservation = performActionWhilePumpingMessages (windowHandle,
                                                                      L"oe.control.acquisition",
                                                                      UiaButtonAction::invoke);
    EXPECT_TRUE (SUCCEEDED (invokeObservation.result));
    EXPECT_EQ (invokeObservation.toggleState, ToggleState_On);
    EXPECT_TRUE (acquisition.getToggleState());
    EXPECT_EQ (acquisitionListener.clickCount.load(), 1);
    EXPECT_TRUE (acquisitionListener.stateAtClick.load());

    const auto toggleObservation = performActionWhilePumpingMessages (windowHandle,
                                                                      L"oe.control.recording",
                                                                      UiaButtonAction::toggle);
    EXPECT_TRUE (SUCCEEDED (toggleObservation.result));
    EXPECT_EQ (toggleObservation.toggleState, ToggleState_On);
    EXPECT_TRUE (recording.getToggleState());
    EXPECT_EQ (recordingListener.clickCount.load(), 1);
    EXPECT_TRUE (recordingListener.stateAtClick.load());

    acquisition.removeListener (&acquisitionListener);
    recording.removeListener (&recordingListener);
    window.setVisible (false);
    window.removeFromDesktop();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
}
#endif
