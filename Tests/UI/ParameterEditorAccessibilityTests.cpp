#include "../../Source/Processors/Parameter/ParameterEditor.h"
#include "../../Source/Processors/Parameter/ParameterOwner.h"
#include "../../Source/UI/SemanticComponent.h"
#include "gtest/gtest.h"
#include <atomic>
#include <thread>

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

namespace
{
class TestParameterOwner final : public ParameterOwner
{
public:
    TestParameterOwner() : ParameterOwner (Type::OTHER) {}

    void parameterChangeRequest (
        Parameter* parameter) override
    {
        parameter->updateValue();
    }
};

class TrackingMessageThreadComboBox final
    : public MessageThreadComboBox
{
public:
    explicit TrackingMessageThreadComboBox (
        std::shared_ptr<std::atomic<int>>
            externalCountToUse = {})
        : externalCount (
              std::move (
                  externalCountToUse))
    {
    }

    void showPopup() override
    {
        showPopupUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        showPopupCount.fetch_add (1);
        if (externalCount != nullptr)
            externalCount->fetch_add (1);
        if (onShowPopup != nullptr)
            onShowPopup();
    }

    std::atomic<bool>
        showPopupUsedMessageThread { false };
    std::atomic<int>
        showPopupCount { 0 };
    std::function<void()> onShowPopup;

private:
    std::shared_ptr<std::atomic<int>>
        externalCount;
};

class TrackingComboBoxValueListener final
    : public ComboBox::Listener
{
public:
    void comboBoxChanged (
        ComboBox* comboBox) override
    {
        callbackCount.fetch_add (1);
        callbackUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
        if (onChange != nullptr)
            onChange (comboBox);
    }

    std::function<void(ComboBox*)>
        onChange;
    std::atomic<int>
        callbackCount { 0 };
    std::atomic<bool>
        callbackUsedMessageThread { false };
};

void joinUiWorkerOrAbort (
    std::thread& worker,
    const std::atomic<bool>&
        workerReturned)
{
    if (! workerReturned.load())
    {
        ADD_FAILURE()
            << "UI worker did not return before its message-loop watchdog expired";
        std::abort();
    }

    worker.join();
}

#if JUCE_WINDOWS
struct WindowsComboBoxValueResult
{
    HRESULT patternResult = E_PENDING;
    HRESULT setValueResult = E_PENDING;
    HRESULT valueResult = E_PENDING;
    HRESULT readOnlyResult = E_PENDING;
    BOOL readOnly = TRUE;
    std::wstring value;
};

WindowsComboBoxValueResult
setWindowsComboBoxValue (
    HWND window,
    const std::wstring& automationId,
    const std::wstring& newValue)
{
    WindowsComboBoxValueResult output;
    const auto comResult =
        CoInitializeEx (
            nullptr,
            COINIT_MULTITHREADED);
    if (FAILED (comResult))
    {
        output.setValueResult =
            comResult;
        return output;
    }

    const auto finish =
        [&]
        {
            CoUninitialize();
            return output;
        };

    Microsoft::WRL::ComPtr<
        IUIAutomation>
        automation;
    auto result =
        CoCreateInstance (
            CLSID_CUIAutomation,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS (
                &automation));
    if (FAILED (result))
    {
        output.setValueResult =
            result;
        return finish();
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        rootElement;
    result =
        automation
            ->ElementFromHandle (
                window,
                &rootElement);
    if (FAILED (result)
        || rootElement == nullptr)
    {
        output.setValueResult =
            FAILED (result)
                ? result
                : E_FAIL;
        return finish();
    }

    VARIANT expectedId;
    VariantInit (
        &expectedId);
    expectedId.vt = VT_BSTR;
    expectedId.bstrVal =
        SysAllocString (
            automationId.c_str());
    if (expectedId.bstrVal
        == nullptr)
    {
        output.setValueResult =
            E_OUTOFMEMORY;
        return finish();
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationCondition>
        idCondition;
    result =
        automation
            ->CreatePropertyCondition (
                UIA_AutomationIdPropertyId,
                expectedId,
                &idCondition);
    VariantClear (
        &expectedId);
    if (FAILED (result))
    {
        output.setValueResult =
            result;
        return finish();
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        element;
    result =
        rootElement
            ->FindFirst (
                TreeScope_Subtree,
                idCondition.Get(),
                &element);
    if (FAILED (result)
        || element == nullptr)
    {
        output.setValueResult =
            FAILED (result)
                ? result
                : E_FAIL;
        return finish();
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationValuePattern>
        valuePattern;
    output.patternResult =
        element
            ->GetCurrentPatternAs (
                UIA_ValuePatternId,
                IID_PPV_ARGS (
                    &valuePattern));
    if (valuePattern == nullptr)
    {
        output.setValueResult =
            FAILED (
                output.patternResult)
                ? output.patternResult
                : E_NOINTERFACE;
        return finish();
    }

    auto valueToSet =
        SysAllocString (
            newValue.c_str());
    if (valueToSet == nullptr)
    {
        output.setValueResult =
            E_OUTOFMEMORY;
        return finish();
    }
    output.setValueResult =
        valuePattern
            ->SetValue (
                valueToSet);
    SysFreeString (
        valueToSet);

    output.readOnlyResult =
        valuePattern
            ->get_CurrentIsReadOnly (
                &output.readOnly);

    BSTR currentValue = nullptr;
    output.valueResult =
        valuePattern
            ->get_CurrentValue (
                &currentValue);
    if (SUCCEEDED (
            output.valueResult)
        && currentValue != nullptr)
    {
        output.value =
            currentValue;
    }
    SysFreeString (
        currentValue);
    return finish();
}
#endif

class ParameterEditorAccessibilityTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        messageManagerLock = std::make_unique<MessageManagerLock>();
    }

    void TearDown() override
    {
        messageManagerLock.reset();
        DeletedAtShutdown::deleteAll();
        MessageManager::deleteInstance();
    }

    static void expectSemanticValueControl (ParameterEditor& editor,
                                            const String& expectedId,
                                            const String& expectedTitle,
                                            const String& expectedDescription)
    {
        auto wrapperHandler =
            editor.createAccessibilityHandler();
        ASSERT_NE (wrapperHandler, nullptr);
        EXPECT_EQ (
            wrapperHandler->getRole(),
            AccessibilityRole::ignored);

        auto* valueControl = editor.getEditor();
        ASSERT_NE (valueControl, nullptr);
        EXPECT_EQ (valueControl->getComponentID(), expectedId);
        EXPECT_EQ (valueControl->getTitle(), expectedTitle);
        EXPECT_EQ (valueControl->getDescription(), expectedDescription);
        EXPECT_TRUE (valueControl->isAccessible());

        auto* visualLabel = editor.getLabel();
        ASSERT_NE (visualLabel, nullptr);
        EXPECT_FALSE (visualLabel->isAccessible());
    }

    static void expectReadOnlyButtonValue (
        ParameterEditor& editor,
        const String& expectedValue)
    {
        auto* valueControl = editor.getEditor();
        ASSERT_NE (valueControl, nullptr);

        auto handler =
            valueControl->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::button);
        ASSERT_NE (
            handler->getValueInterface(),
            nullptr);
        EXPECT_TRUE (
            handler->getValueInterface()
                ->isReadOnly());
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            expectedValue);
    }

    std::unique_ptr<MessageManagerLock> messageManagerLock;
};
} // namespace

TEST_F (ParameterEditorAccessibilityTests,
        ComboBoxActionsUseMessageThreadAndHonourDisabledState)
{
    TrackingMessageThreadComboBox comboBox;
    comboBox.addItem ("Binary", 1);
    comboBox.addItem ("NWB", 2);
    comboBox.setSelectedId (
        1,
        dontSendNotification);
    comboBox.synchroniseAccessibilityState();

    auto handler =
        comboBox.createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::comboBox);
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::press));
    EXPECT_TRUE (
        handler->getActions().contains (
            AccessibilityActionType::showMenu));

    AccessibleState workerState;
    std::thread stateReader (
        [&]
        {
            workerState =
                handler->getCurrentState();
        });
    stateReader.join();
    EXPECT_TRUE (
        workerState.isExpandable());
    EXPECT_TRUE (
        workerState.isCollapsed());

    std::atomic<bool> invoked { false };
    std::thread worker (
        [&]
        {
            invoked.store (
                handler->getActions().invoke (
                    AccessibilityActionType::
                        showMenu));
        });

    while (! invoked.load())
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    worker.join();

    EXPECT_TRUE (
        comboBox
            .showPopupUsedMessageThread
            .load());
    EXPECT_EQ (
        comboBox.showPopupCount.load(),
        1);

    comboBox.setEnabled (false);
    EXPECT_FALSE (
        handler->isEnabled());
    invoked.store (false);
    std::thread disabledWorker (
        [&]
        {
            invoked.store (
                handler->getActions().invoke (
                    AccessibilityActionType::
                        press));
        });

    while (! invoked.load())
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    disabledWorker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_EQ (
        comboBox.showPopupCount.load(),
        1);
}

TEST_F (ParameterEditorAccessibilityTests,
        CopiedComboBoxActionsDoNotOutliveTheControl)
{
    auto externalCount =
        std::make_shared<std::atomic<int>> (
            0);
    std::function<bool()> invokeShowMenu;

    {
        auto comboBox =
            std::make_unique<
                TrackingMessageThreadComboBox> (
                externalCount);
        auto handler =
            comboBox
                ->createAccessibilityHandler();
        const auto actions =
            handler->getActions();
        invokeShowMenu =
            [actions]() mutable
            {
                return actions.invoke (
                    AccessibilityActionType::
                        showMenu);
            };
    }

    std::atomic<bool> invoked { false };
    std::thread worker (
        [&]
        {
            invoked.store (
                invokeShowMenu());
        });

    while (! invoked.load())
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    worker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_EQ (
        externalCount->load(),
        0);
}

TEST_F (ParameterEditorAccessibilityTests,
        ComboBoxCanBeDestroyedWhileShowMenuRuns)
{
    auto comboBox =
        std::make_unique<
            TrackingMessageThreadComboBox>();
    auto handler =
        comboBox
            ->createAccessibilityHandler();
    const auto actions =
        handler->getActions();
    comboBox->onShowPopup =
        [&]
        {
            handler.reset();
            comboBox.reset();
        };

    std::atomic<bool> invoked { false };
    std::thread worker (
        [&]
        {
            invoked.store (
                actions.invoke (
                    AccessibilityActionType::
                        showMenu));
        });

    while (! invoked.load())
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    worker.join();

    EXPECT_TRUE (invoked.load());
    EXPECT_EQ (comboBox, nullptr);
    EXPECT_EQ (handler, nullptr);
}

TEST_F (ParameterEditorAccessibilityTests,
        PublishesComboBoxExpandedAndCollapsedState)
{
    MessageThreadComboBox comboBox;
    comboBox.addItem ("Binary", 1);
    comboBox.setSelectedId (
        1,
        dontSendNotification);
    comboBox.synchroniseAccessibilityState();
    auto handler =
        comboBox
            .createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();

    EXPECT_TRUE (
        handler->getCurrentState()
            .isCollapsed());

    comboBox.showPopup();
    EXPECT_TRUE (
        comboBox.isPopupActive());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isExpanded());

    comboBox.hidePopup();
    comboBox
        .synchroniseAccessibilityState();
    EXPECT_TRUE (
        handler->getCurrentState()
            .isCollapsed());

    EXPECT_TRUE (
        actions.contains (
            AccessibilityActionType::
                expand));
    EXPECT_TRUE (
        actions.contains (
            AccessibilityActionType::
                collapse));
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                expand));
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                expand));
    EXPECT_TRUE (
        comboBox.isPopupActive());
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                collapse));
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                collapse));
    EXPECT_FALSE (
        comboBox.isPopupActive());
}

TEST_F (ParameterEditorAccessibilityTests,
        ShowMenuActionTogglesComboBoxExpandedState)
{
    MessageThreadComboBox comboBox;
    comboBox.addItem ("Binary", 1);
    comboBox.setSelectedId (
        1,
        dontSendNotification);
    comboBox.synchroniseAccessibilityState();
    auto handler =
        comboBox
            .createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    const auto actions =
        handler->getActions();
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                showMenu));
    EXPECT_TRUE (
        comboBox.isPopupActive());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isExpanded());

    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                showMenu));
    EXPECT_FALSE (
        comboBox.isPopupActive());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isCollapsed());
}

TEST_F (ParameterEditorAccessibilityTests,
        DirectionalComboBoxActionsHonourLatestState)
{
    MessageThreadComboBox comboBox;
    comboBox.addItem ("Binary", 1);
    comboBox.setSelectedId (
        1,
        dontSendNotification);
    comboBox.synchroniseAccessibilityState();
    auto handler =
        comboBox
            .createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();

    const auto invokeFromWorker =
        [&] (
            AccessibilityActionType action,
            const std::function<void()>&
                changeStateBeforeDispatch)
    {
        std::atomic<bool>
            workerStarted { false };
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                workerStarted.store (
                    true);
                actions.invoke (
                    action);
                workerReturned.store (
                    true);
            });
        while (! workerStarted.load())
            std::this_thread::yield();

        changeStateBeforeDispatch();
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();
        EXPECT_TRUE (
            workerReturned.load());
    };

    invokeFromWorker (
        AccessibilityActionType::expand,
        [&]
        {
            comboBox.showPopup();
        });
    EXPECT_TRUE (
        comboBox.isPopupActive());

    invokeFromWorker (
        AccessibilityActionType::collapse,
        [&]
        {
            comboBox.hidePopup();
            comboBox
                .synchroniseAccessibilityState();
        });
    EXPECT_FALSE (
        comboBox.isPopupActive());
}

TEST_F (ParameterEditorAccessibilityTests,
        EditableComboBoxValueWritesUseMessageThreadAndHonourState)
{
    MessageThreadComboBox comboBox;
    comboBox.addItem ("0.5", 1);
    comboBox.setSelectedId (
        1,
        dontSendNotification);
    comboBox.synchroniseAccessibilityState();
    TrackingComboBoxValueListener
        listener;
    comboBox.addListener (
        &listener);

    auto handler =
        comboBox
            .createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value =
        handler
            ->getValueInterface();
    ASSERT_NE (value, nullptr);
    EXPECT_TRUE (
        value->isReadOnly());

    comboBox.setEditableText (
        true);
    EXPECT_FALSE (
        value->isReadOnly());

    static_cast<ComboBox&> (
        comboBox)
        .setEditableText (
            false);
    EXPECT_TRUE (
        value->isReadOnly());
    static_cast<ComboBox&> (
        comboBox)
        .setEditableText (
            true);
    EXPECT_FALSE (
        value->isReadOnly());

    const auto setValueFromWorker =
        [&] (const String& text)
    {
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                value->setValueAsString (
                    text);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();
        EXPECT_TRUE (
            workerReturned.load());
    };

    setValueFromWorker (
        "0.75");
    EXPECT_EQ (
        comboBox.getText(),
        "0.75");
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "0.75");
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());

    listener.onChange =
        [] (ComboBox* changed)
        {
            if (changed->getText()
                == "0")
            {
                changed->setText (
                    "0.05",
                    dontSendNotification);
            }
        };
    setValueFromWorker (
        "0");
    EXPECT_EQ (
        comboBox.getText(),
        "0.05");
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "0.05");
    EXPECT_EQ (
        listener.callbackCount.load(),
        2);

    comboBox.setEnabled (
        false);
    setValueFromWorker (
        "1.25");
    EXPECT_EQ (
        comboBox.getText(),
        "0.05");
    EXPECT_EQ (
        listener.callbackCount.load(),
        2);

    comboBox.removeListener (
        &listener);
}

TEST_F (ParameterEditorAccessibilityTests,
        ExistingComboBoxItemsCanOptInToExactAccessibilitySelection)
{
    MessageThreadComboBox comboBox;
    comboBox.addItem ("1", 1);
    comboBox.addItem (
        "By Shank",
        2);
    comboBox.setSelectedId (
        1,
        dontSendNotification);
    comboBox
        .synchroniseAccessibilityState();

    auto handler =
        comboBox
            .createAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    auto* value =
        handler
            ->getValueInterface();
    ASSERT_NE (
        value,
        nullptr);
    EXPECT_TRUE (
        value->isReadOnly());
    EXPECT_FALSE (
        comboBox
            .isTextEditable());

    comboBox
        .setAccessibilityValueSelectionEnabled (
            true);
    EXPECT_FALSE (
        value->isReadOnly());
    EXPECT_FALSE (
        comboBox
            .isTextEditable());
    auto* validatedWriter =
        dynamic_cast<
            AccessibilityValueStringWriter*> (
            value);
    ASSERT_NE (
        validatedWriter,
        nullptr);

    TrackingComboBoxValueListener
        listener;
    comboBox.addListener (
        &listener);
    const auto setValueFromWorker =
        [&] (const String& text)
    {
        bool accepted = false;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                accepted =
                    validatedWriter
                        ->setValueAsStringIfSupported (
                            text);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinUiWorkerOrAbort (
            worker,
            workerReturned);
        return accepted;
    };

    EXPECT_TRUE (
        setValueFromWorker (
            "By Shank"));
    EXPECT_EQ (
        comboBox.getSelectedId(),
        2);
    EXPECT_EQ (
        comboBox.getText(),
        "By Shank");
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());

    EXPECT_FALSE (
        setValueFromWorker (
            "unsupported"));
    EXPECT_EQ (
        comboBox.getSelectedId(),
        2);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);

    comboBox.setItemEnabled (
        1,
        false);
    EXPECT_FALSE (
        setValueFromWorker (
            "1"));
    EXPECT_EQ (
        comboBox.getSelectedId(),
        2);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    comboBox.setItemEnabled (
        1,
        true);

    comboBox.setEnabled (
        false);
    EXPECT_FALSE (
        setValueFromWorker (
            "1"));
    EXPECT_EQ (
        comboBox.getSelectedId(),
        2);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);

    comboBox.setEnabled (
        true);
    comboBox
        .setAccessibilityValueSelectionEnabled (
            false);
    EXPECT_TRUE (
        value->isReadOnly());
    EXPECT_FALSE (
        setValueFromWorker (
            "1"));
    EXPECT_EQ (
        comboBox.getSelectedId(),
        2);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);

    comboBox.removeListener (
        &listener);
}

#if JUCE_WINDOWS
TEST_F (ParameterEditorAccessibilityTests,
        WindowsValuePatternWritesEditableComboBoxAndReportsStateFailures)
{
    Component host;
    host.setSize (320, 120);
    host.addToDesktop (0);
    host.setVisible (true);
    ASSERT_TRUE (host.isShowing());

    MessageThreadComboBox comboBox;
    comboBox.setComponentID (
        "oe.test.editable_combo");
    comboBox.setTitle (
        "Editable test value");
    comboBox.setDescription (
        "Set an editable test value.");
    comboBox.setHelpText (
        "Set an editable test value.");
    comboBox.setEditableText (
        true);
    comboBox.setText (
        "0.5",
        dontSendNotification);
    comboBox.setBounds (
        10,
        10,
        200,
        24);
    host.addAndMakeVisible (
        comboBox);
    comboBox.synchroniseAccessibilityState();

    TrackingComboBoxValueListener
        listener;
    listener.onChange =
        [] (ComboBox* changed)
        {
            if (changed->getText()
                == "0")
            {
                changed->setText (
                    "0.05",
                    dontSendNotification);
            }
        };
    comboBox.addListener (
        &listener);

    const auto window =
        static_cast<HWND> (
            host.getWindowHandle());
    ASSERT_NE (window, nullptr);
    const std::wstring id (
        comboBox
            .getComponentID()
            .toWideCharPointer());

    const auto setFromWindows =
        [&] (
            const std::wstring& newValue)
    {
        WindowsComboBoxValueResult
            result;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                result =
                    setWindowsComboBoxValue (
                        window,
                        id,
                        newValue);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 200
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();
        EXPECT_TRUE (
            workerReturned.load());
        return result;
    };

    const auto writeResult =
        setFromWindows (
            L"0");
    EXPECT_EQ (
        writeResult.patternResult,
        S_OK);
    EXPECT_EQ (
        writeResult.setValueResult,
        S_OK);
    EXPECT_EQ (
        writeResult.readOnlyResult,
        S_OK);
    EXPECT_EQ (
        writeResult.readOnly,
        FALSE);
    EXPECT_EQ (
        writeResult.valueResult,
        S_OK);
    EXPECT_EQ (
        writeResult.value,
        L"0.05");
    EXPECT_EQ (
        comboBox.getText(),
        "0.05");
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());

    comboBox.setEnabled (
        false);
    const auto disabledResult =
        setFromWindows (
            L"1.0");
    EXPECT_EQ (
        disabledResult.patternResult,
        S_OK);
    EXPECT_EQ (
        disabledResult.setValueResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_EQ (
        comboBox.getText(),
        "0.05");
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);

    comboBox.setEnabled (
        true);
    static_cast<ComboBox&> (
        comboBox)
        .setEditableText (
            false);
    const auto readOnlyResult =
        setFromWindows (
            L"1.0");
    EXPECT_EQ (
        readOnlyResult.patternResult,
        S_OK);
    EXPECT_EQ (
        readOnlyResult.setValueResult,
        static_cast<HRESULT> (
            UIA_E_INVALIDOPERATION));
    EXPECT_EQ (
        readOnlyResult.readOnlyResult,
        S_OK);
    EXPECT_EQ (
        readOnlyResult.readOnly,
        TRUE);
    EXPECT_EQ (
        comboBox.getText(),
        "0.05");
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);

    comboBox.removeListener (
        &listener);
}

TEST_F (ParameterEditorAccessibilityTests,
        WindowsValuePatternSurvivesComboBoxDestructionDuringWrite)
{
    Component host;
    host.setSize (320, 120);
    host.addToDesktop (0);
    host.setVisible (true);
    ASSERT_TRUE (host.isShowing());

    auto comboBox =
        std::make_unique<
            MessageThreadComboBox>();
    comboBox->setComponentID (
        "oe.test.destroyed_editable_combo");
    comboBox->setTitle (
        "Destroyable editable test value");
    comboBox->setEditableText (
        true);
    comboBox->setText (
        "0.5",
        dontSendNotification);
    comboBox->setBounds (
        10,
        10,
        200,
        24);
    host.addAndMakeVisible (
        *comboBox);
    comboBox
        ->synchroniseAccessibilityState();

    TrackingComboBoxValueListener
        listener;
    listener.onChange =
        [&] (ComboBox*)
        {
            comboBox.reset();
        };
    comboBox->addListener (
        &listener);

    const auto window =
        static_cast<HWND> (
            host.getWindowHandle());
    ASSERT_NE (window, nullptr);
    const std::wstring id =
        L"oe.test.destroyed_editable_combo";

    WindowsComboBoxValueResult
        result;
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            result =
                setWindowsComboBoxValue (
                    window,
                    id,
                    L"1.0");
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 200
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    worker.join();

    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_EQ (
        result.patternResult,
        S_OK);
    EXPECT_EQ (
        result.setValueResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
    EXPECT_EQ (
        comboBox,
        nullptr);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
}
#endif

TEST_F (ParameterEditorAccessibilityTests,
        ExposesRecordEngineSelectionAndExternalUpdates)
{
    TestParameterOwner owner;
    Array<String> engines {
        "Binary",
        "NWB"
    };
    CategoricalParameter parameter (
        &owner,
        Parameter::PROCESSOR_SCOPE,
        "engine",
        "Engine",
        "Recording data format",
        engines,
        0);
    parameter.setKey ("100|engine");

    ComboBoxParameterEditor editor (
        &parameter);
    expectSemanticValueControl (
        editor,
        "oe.parameter.100_engine",
        "Engine",
        "Recording data format");

    auto* comboBox =
        dynamic_cast<
            MessageThreadComboBox*> (
            editor.getEditor());
    ASSERT_NE (comboBox, nullptr);
    auto handler =
        comboBox
            ->createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_EQ (
        handler->getRole(),
        AccessibilityRole::comboBox);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);
    EXPECT_TRUE (
        handler->getValueInterface()
            ->isReadOnly());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Binary");

    parameter.setNextValue (
        1,
        false);
    editor.updateView();

    String workerValue;
    String workerTitle;
    String workerDescription;
    String workerHelp;
    std::thread reader (
        [&]
        {
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
            workerTitle =
                handler->getTitle();
            workerDescription =
                handler->getDescription();
            workerHelp =
                handler->getHelp();
        });
    reader.join();
    EXPECT_EQ (
        workerValue,
        "NWB");
    EXPECT_EQ (
        workerTitle,
        "Engine");
    EXPECT_EQ (
        workerDescription,
        "Recording data format");
    EXPECT_EQ (
        workerHelp,
        "Recording data format");
}

TEST_F (ParameterEditorAccessibilityTests, AppliesSemanticMetadataToEverySharedValueControl)
{
    {
        BooleanParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "enabled", "Enabled", "Enables processing.", false);
        parameter.setKey ("101|enabled");
        ToggleParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_enabled", "Enabled", "Enables processing.");
    }

    {
        StringParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "label", "Label", "Sets the label.", "default");
        parameter.setKey ("101|label");
        TextBoxParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_label", "Label", "Sets the label.");
    }

    {
        Array<String> categories { "First", "Second" };
        CategoricalParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "mode", "Mode", "Selects the mode.", categories, 0);
        parameter.setKey ("101|mode");
        ComboBoxParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_mode", "Mode", "Selects the mode.");
    }

    {
        IntParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "count", "Count", "Sets the count.", 4, 0, 8);
        parameter.setKey ("101|count");
        BoundedValueParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_count", "Count", "Sets the count.");
    }

    {
        Array<var> selectedChannels;
        SelectedChannelsParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "channels", "Channels", "Selects channels.", selectedChannels);
        parameter.setKey ("101|channels");
        parameter.setChannelCount (4);
        SelectedChannelsParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_channels", "Channels", "Selects channels.");
    }

    {
        MaskChannelsParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "mask", "Mask", "Masks channels.");
        parameter.setKey ("101|mask");
        parameter.setChannelCount (4);
        MaskChannelsParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_mask", "Mask", "Masks channels.");
    }

    {
        Array<String> streams { "Stream A", "Stream B" };
        SelectedStreamParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "stream", "Stream", "Selects a stream.", streams, 0);
        parameter.setKey ("101|stream");
        SelectedStreamParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_stream", "Stream", "Selects a stream.");
    }

    {
        TtlLineParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "ttl_line", "TTL line", "Selects a TTL line.");
        parameter.setKey ("101|ttl_line");
        TtlLineParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_ttl_line", "TTL line", "Selects a TTL line.");
    }

    {
        PathParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "directory", "Directory", "Selects a directory.", File(), {}, true, false);
        parameter.setKey ("101|directory");
        PathParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_directory", "Directory", "Selects a directory.");
    }

    {
        TimeParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "start_time", "Start time", "Sets the start time.", "00:00:00.000");
        parameter.setKey ("101|start_time");
        TimeParameterEditor editor (&parameter);
        expectSemanticValueControl (editor, "oe.parameter.101_start_time", "Start time", "Sets the start time.");
    }
}

TEST_F (ParameterEditorAccessibilityTests, RefreshesSemanticMetadataWhenAControlIsReused)
{
    StringParameter placeholder (nullptr, Parameter::GLOBAL_SCOPE, "unknown", "Unknown", "Placeholder parameter.", "");
    StringParameter parameter (nullptr, Parameter::GLOBAL_SCOPE, "channel", "Channel", "Selects the monitored channel.", "1");
    parameter.setKey ("106|channel");

    TextBoxParameterEditor editor (&placeholder);
    editor.setParameter (&parameter);

    expectSemanticValueControl (editor,
                                "oe.parameter.106_channel",
                                "Channel",
                                "Selects the monitored channel.");
}

TEST_F (ParameterEditorAccessibilityTests,
        ExposesCurrentValuesForButtonBasedEditors)
{
    TestParameterOwner owner;

    {
        Array<var> selected { 0, 2 };
        SelectedChannelsParameter parameter (
            &owner,
            Parameter::GLOBAL_SCOPE,
            "channels",
            "Channels",
            "Selects channels.",
            selected);
        parameter.setKey ("101|channels");
        parameter.setChannelCount (4);
        SelectedChannelsParameterEditor editor (
            &parameter);
        expectReadOnlyButtonValue (editor, "1, 3");
        auto liveHandler =
            editor.getEditor()
                ->createAccessibilityHandler();
        ASSERT_NE (liveHandler, nullptr);
        ASSERT_NE (
            liveHandler->getValueInterface(),
            nullptr);

        Array<var> updated { 1 };
        parameter.setNextValue (updated, false);
        EXPECT_EQ (
            liveHandler->getValueInterface()
                ->getCurrentValueAsString(),
            "2");
    }

    {
        MaskChannelsParameter parameter (
            &owner,
            Parameter::GLOBAL_SCOPE,
            "mask",
            "Mask",
            "Masks channels.");
        parameter.setKey ("101|mask");
        parameter.setChannelCount (4);
        MaskChannelsParameterEditor editor (&parameter);
        expectReadOnlyButtonValue (editor, "4/4");
    }

    {
        TtlLineParameter parameter (
            &owner,
            Parameter::STREAM_SCOPE,
            "ttl_line",
            "TTL line",
            "Selects a TTL line.");
        parameter.setKey ("101|ttl_line");
        TtlLineParameterEditor editor (&parameter);
        expectReadOnlyButtonValue (editor, "Line 1");
    }

    {
        const auto directory =
            File::getSpecialLocation (
                File::tempDirectory);
        PathParameter parameter (
            &owner,
            Parameter::GLOBAL_SCOPE,
            "directory",
            "Directory",
            "Selects a directory.",
            directory,
            StringArray {},
            true,
            true);
        parameter.setKey ("101|directory");
        PathParameterEditor editor (&parameter);
        editor.updateView();
        expectReadOnlyButtonValue (
            editor,
            directory.getFullPathName());
    }

    {
        TimeParameter parameter (
            &owner,
            Parameter::GLOBAL_SCOPE,
            "start_time",
            "Start time",
            "Sets the start time.",
            "00:00:01.000");
        parameter.setKey ("101|start_time");
        TimeParameterEditor editor (&parameter);
        expectReadOnlyButtonValue (
            editor,
            "00:00:01.000");
    }
}

TEST_F (ParameterEditorAccessibilityTests,
        ExposesSynchronizationStatusAndClockRole)
{
    SynchronizingProcessor processor;
    processor.synchronizer.addDataStream (
        "main",
        30000.0f);
    processor.synchronizer.addDataStream (
        "secondary",
        30000.0f);
    processor.setMainDataStream ("main");

    SyncControlButton mainButton (
        &processor,
        "Main synchronization",
        "main");
    SyncControlButton secondaryButton (
        &processor,
        "Secondary synchronization",
        "secondary");

    auto mainHandler =
        static_cast<Component&> (mainButton)
            .createAccessibilityHandler();
    auto secondaryHandler =
        static_cast<Component&> (secondaryButton)
            .createAccessibilityHandler();
    ASSERT_NE (mainHandler, nullptr);
    ASSERT_NE (secondaryHandler, nullptr);
    EXPECT_EQ (
        mainHandler->getRole(),
        AccessibilityRole::button);
    EXPECT_TRUE (
        mainHandler->getActions().contains (
            AccessibilityActionType::press));
    ASSERT_NE (
        mainHandler->getValueInterface(),
        nullptr);
    ASSERT_NE (
        secondaryHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        mainHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "off; main clock");
    EXPECT_EQ (
        secondaryHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "off; secondary clock");

    processor.synchronizer.startAcquisition();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (300);
    EXPECT_EQ (
        mainHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "synchronized; main clock");
    EXPECT_EQ (
        secondaryHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "synchronizing; secondary clock");
    processor.synchronizer.stopAcquisition();

    SynchronizingProcessor hardwareProcessor;
    hardwareProcessor.synchronizer.addDataStream (
        "hardware",
        30000.0f,
        -1,
        true);
    SyncControlButton hardwareButton (
        &hardwareProcessor,
        "Hardware synchronization",
        "hardware");
    auto hardwareHandler =
        static_cast<Component&> (hardwareButton)
            .createAccessibilityHandler();
    ASSERT_NE (hardwareHandler, nullptr);
    ASSERT_NE (
        hardwareHandler->getValueInterface(),
        nullptr);
    EXPECT_EQ (
        hardwareHandler->getValueInterface()
            ->getCurrentValueAsString(),
        "hardware synchronized; secondary clock");
}

TEST_F (ParameterEditorAccessibilityTests,
        SynchronizationStatusUsesAMessageThreadSnapshot)
{
    SynchronizingProcessor processor;
    processor.synchronizer.addDataStream (
        "main",
        30000.0f);
    processor.setMainDataStream ("main");
    SyncControlButton button (
        &processor,
        "Main synchronization",
        "main");
    auto handler =
        static_cast<Component&> (button)
            .createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);

    String workerValue;
    std::thread initialReader (
        [&]
        {
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
        });
    initialReader.join();
    EXPECT_EQ (
        workerValue,
        "off; main clock");

    processor.synchronizer.startAcquisition();
    std::thread prePublishReader (
        [&]
        {
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
        });
    prePublishReader.join();
    EXPECT_EQ (
        workerValue,
        "off; main clock");

    MessageManager::getInstance()
        ->runDispatchLoopUntil (300);
    std::thread publishedReader (
        [&]
        {
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
        });
    publishedReader.join();
    EXPECT_EQ (
        workerValue,
        "synchronized; main clock");
}

TEST_F (ParameterEditorAccessibilityTests,
        SynchronizationControlActionAndProvidersAreWorkerSafe)
{
    SynchronizingProcessor processor;
    processor.synchronizer.addDataStream (
        "main",
        30000.0f);
    processor.setMainDataStream ("main");
    auto button =
        std::make_unique<SyncControlButton> (
            &processor,
            "Main synchronization",
            "main");
    applySemanticMetadata (
        *button,
        "oe.processor.100.parameter.synchronization",
        "Main synchronization",
        "Configure synchronization for the main stream.",
        "Configure synchronization for the main stream.");
    auto handler =
        static_cast<Component&> (*button)
            .createAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    ASSERT_NE (
        handler->getValueInterface(),
        nullptr);

    String workerTitle;
    String workerDescription;
    String workerHelp;
    String workerValue;
    AccessibleState workerState;
    bool workerEnabled = false;
    std::thread reader (
        [&]
        {
            workerTitle =
                handler->getTitle();
            workerDescription =
                handler->getDescription();
            workerHelp =
                handler->getHelp();
            workerValue =
                handler->getValueInterface()
                    ->getCurrentValueAsString();
            workerState =
                handler->getCurrentState();
            workerEnabled =
                handler->isEnabled();
        });
    reader.join();

    EXPECT_EQ (
        workerTitle,
        "Main synchronization");
    EXPECT_EQ (
        workerDescription,
        "Configure synchronization for the main stream.");
    EXPECT_EQ (
        workerHelp,
        workerDescription);
    EXPECT_EQ (
        workerValue,
        "off; main clock");
    EXPECT_TRUE (
        workerState.isFocusable());
    EXPECT_TRUE (workerEnabled);

    auto clickCount =
        std::make_shared<
            std::atomic<int>> (0);
    std::atomic<bool>
        clickUsedMessageThread { false };
    button->onClick =
        [clickCount,
         &clickUsedMessageThread]
        {
            clickCount->fetch_add (1);
            clickUsedMessageThread.store (
                MessageManager::getInstance()
                    ->isThisTheMessageThread());
        };
    const auto actions =
        handler->getActions();
    const auto invokePress =
        [&actions]
        {
            std::atomic<bool>
                invoked { false };
            std::thread worker (
                [&]
                {
                    invoked.store (
                        actions.invoke (
                            AccessibilityActionType::
                                press));
                });
            while (! invoked.load())
            {
                MessageManager::getInstance()
                    ->runDispatchLoopUntil (10);
            }
            worker.join();
            return invoked.load();
        };

    ASSERT_TRUE (invokePress());
    for (int attempt = 0;
         attempt < 20
             && clickCount->load() == 0;
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    EXPECT_EQ (clickCount->load(), 1);
    EXPECT_TRUE (
        clickUsedMessageThread.load());

    button->setEnabled (false);
    EXPECT_FALSE (handler->isEnabled());
    EXPECT_TRUE (invokePress());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (20);
    EXPECT_EQ (clickCount->load(), 1);

    handler.reset();
    button.reset();
    EXPECT_TRUE (invokePress());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (20);
    EXPECT_EQ (clickCount->load(), 1);
}
