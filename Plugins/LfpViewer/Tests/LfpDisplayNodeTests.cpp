/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>

#include "gtest/gtest.h"

#include "../DisplayBuffer.h"
#include "../LfpDisplayCanvas.h"
#include "../LfpDisplay.h"
#include "../LfpChannelDisplayInfo.h"
#include "../LfpDisplayEditor.h"
#include "../LfpDisplayNode.h"
#include <ModelApplication.h>
#include <ModelProcessors.h>
#include <ProcessorHeaders.h>
#include <TestFixtures.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <thread>
#include <tuple>

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

namespace
{
class LfpGuiRuntimeEnvironment final
    : public testing::Environment
{
public:
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

[[maybe_unused]]
testing::Environment* const
    lfpGuiRuntimeEnvironment =
        testing::
            AddGlobalTestEnvironment (
                new LfpGuiRuntimeEnvironment());

Component* findLfpDescendantById (
    Component& parent,
    StringRef id)
{
    if (parent.getComponentID()
        == id)
    {
        return &parent;
    }

    for (auto* child :
         parent.getChildren())
    {
        if (auto* result =
                findLfpDescendantById (
                    *child,
                    id))
        {
            return result;
        }
    }

    return nullptr;
}

Component* findLfpDescendantByAccessibilityTitle (
    Component& parent,
    StringRef title)
{
    if (auto* handler =
            parent.getAccessibilityHandler())
    {
        if (handler->getTitle()
            == title)
        {
            return &parent;
        }
    }

    for (auto* child :
         parent.getChildren())
    {
        if (auto* result =
                findLfpDescendantByAccessibilityTitle (
                    *child,
                    title))
        {
            return result;
        }
    }

    return nullptr;
}

Component* findLfpDesktopComponentByAccessibilityTitle (
    StringRef title)
{
    auto& desktop =
        Desktop::getInstance();

    for (int index = 0;
         index < desktop
                     .getNumComponents();
         ++index)
    {
        if (auto* component =
                desktop.getComponent (
                    index))
        {
            if (auto* result =
                    findLfpDescendantByAccessibilityTitle (
                        *component,
                        title))
            {
                return result;
            }
        }
    }

    return nullptr;
}

class LfpChannelSelectionProbe final
    : public LfpViewer::
          LfpChannelDisplay
{
public:
    static bool read (
        const LfpViewer::
            LfpChannelDisplay&
            channel)
    {
        const auto selectedMember =
            &LfpChannelSelectionProbe::
                isSelected;
        return channel.*selectedMember;
    }
};

Array<String> drainLfpBroadcastMessages (
    MessageCenter& messageCenter)
{
    AudioBuffer<float> audioBuffer (
        1,
        1);
    audioBuffer.clear();
    MidiBuffer eventBuffer;
    static_cast<AudioProcessor&> (
        messageCenter)
        .processBlock (
            audioBuffer,
            eventBuffer);

    Array<String> messages;
    for (const auto metadata :
         eventBuffer)
    {
        if (auto event =
                TextEvent::deserialize (
                    metadata.data,
                    messageCenter
                        .getMessageChannel()))
        {
            messages.add (
                event->getText());
        }
    }
    return messages;
}

template <typename ComponentType>
ComponentType* findLfpDescendant (
    Component& parent)
{
    if (auto* result =
            dynamic_cast<
                ComponentType*> (
                &parent))
    {
        return result;
    }

    for (auto* child :
         parent.getChildren())
    {
        if (auto* result =
                findLfpDescendant<
                    ComponentType> (
                    *child))
        {
            return result;
        }
    }

    return nullptr;
}

template <typename ComponentType>
void collectLfpDescendants (
    Component& parent,
    std::vector<ComponentType*>&
        results)
{
    if (auto* result =
            dynamic_cast<
                ComponentType*> (
                &parent))
    {
        results.push_back (
            result);
    }

    for (auto* child :
         parent.getChildren())
    {
        collectLfpDescendants<
            ComponentType> (
            *child,
            results);
    }
}

template <typename ComponentType>
ComponentType* findLfpAncestor (
    Component& component)
{
    for (auto* parent =
             component
                 .getParentComponent();
         parent != nullptr;
         parent =
             parent
                 ->getParentComponent())
    {
        if (auto* result =
                dynamic_cast<
                    ComponentType*> (
                    parent))
        {
            return result;
        }
    }

    return nullptr;
}

class LfpThreadTrackingButtonListener final
    : public Button::Listener
{
public:
    void buttonClicked (Button*) override
    {
        callbackCount.fetch_add (1);
        callbackUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
    }

    std::atomic<int> callbackCount { 0 };
    std::atomic<bool>
        callbackUsedMessageThread { false };
};

class LfpThreadTrackingComboBoxListener final
    : public ComboBox::Listener
{
public:
    void comboBoxChanged (
        ComboBox*) override
    {
        callbackCount.fetch_add (1);
        callbackUsedMessageThread.store (
            MessageManager::getInstance()
                ->isThisTheMessageThread());
    }

    std::atomic<int> callbackCount { 0 };
    std::atomic<bool>
        callbackUsedMessageThread { false };
};

class LfpDestroyCanvasButtonListener final
    : public Button::Listener
{
public:
    explicit LfpDestroyCanvasButtonListener (
        std::unique_ptr<
            LfpViewer::
                LfpDisplayCanvas>&
            canvasToDestroy)
        : canvas (
              canvasToDestroy)
    {
    }

    void buttonClicked (Button*) override
    {
        canvas.reset();
    }

private:
    std::unique_ptr<
        LfpViewer::
            LfpDisplayCanvas>&
        canvas;
};

class LfpDestroyCanvasComboBoxListener final
    : public ComboBox::Listener
{
public:
    explicit LfpDestroyCanvasComboBoxListener (
        std::unique_ptr<
            LfpViewer::
                LfpDisplayCanvas>&
            canvasToDestroy)
        : canvas (
              canvasToDestroy)
    {
    }

    void comboBoxChanged (
        ComboBox*) override
    {
        canvas.reset();
    }

private:
    std::unique_ptr<
        LfpViewer::
            LfpDisplayCanvas>&
        canvas;
};

struct LfpStreamRemovalSnapshotState
{
    int callbackCount = 0;
    int lastMembershipCount = -1;
};

class LfpStreamRemovalSnapshotListener final
    : public ComponentListener
{
public:
    LfpStreamRemovalSnapshotListener (
        LfpViewer::LfpDisplaySplitter&
            splitterToObserve,
        LfpViewer::DisplayBuffer&
            removedBufferToObserve,
        AccessibilityValueInterface&
            valueToObserve,
        LfpStreamRemovalSnapshotState&
            sharedStateToUse)
        : splitter (
              splitterToObserve),
          removedBuffer (
              removedBufferToObserve),
          value (
              valueToObserve),
          sharedState (
              sharedStateToUse)
    {
    }

    void componentEnablementChanged (
        Component& component) override
    {
        if (component.isEnabled())
            return;

        ++sharedState.callbackCount;
        sharedState
            .lastMembershipCount =
            removedBuffer
                .displays
                .size();
        pointerWasNull =
            splitter.displayBuffer
            == nullptr;
        keyWasEmpty =
            splitter
                .selectedStreamKey
                .isEmpty();
        idWasZero =
            splitter.selectedStreamId
            == 0;
        comboIdWasZero =
            splitter
                .streamSelection
                ->getSelectedId()
            == 0;
        comboText =
            splitter
                .streamSelection
                ->getText();
        accessibleValue =
            value
                .getCurrentValueAsString();
    }

    bool pointerWasNull = false;
    bool keyWasEmpty = false;
    bool idWasZero = false;
    bool comboIdWasZero = false;
    String comboText;
    String accessibleValue;

private:
    LfpViewer::LfpDisplaySplitter&
        splitter;
    LfpViewer::DisplayBuffer&
        removedBuffer;
    AccessibilityValueInterface&
        value;
    LfpStreamRemovalSnapshotState&
        sharedState;
};

void joinLfpWorkerOrAbort (
    std::thread& worker,
    const std::atomic<bool>&
        workerReturned)
{
    if (! workerReturned.load())
    {
        ADD_FAILURE()
            << "LFP worker did not return before its message-loop watchdog expired";
        std::abort();
    }

    worker.join();
}

#if JUCE_WINDOWS
enum class LfpWindowsUiaAction
{
    invoke,
    queryInvoke,
    toggle,
    queryToggle,
    select,
    removeFromSelection,
    querySelection,
    expand,
    collapse,
    queryExpansion,
    queryValue,
    setValue
};

struct LfpWindowsUiaInvokeResult
{
    HRESULT invokeResult = E_PENDING;
    HRESULT focusResult = E_PENDING;
    HRESULT invokePatternResult =
        E_PENDING;
    HRESULT togglePatternResult =
        E_PENDING;
    HRESULT selectionItemPatternResult =
        E_PENDING;
    HRESULT expandCollapsePatternResult =
        E_PENDING;
    HRESULT valuePatternResult =
        E_PENDING;
    bool invokePatternAvailable = false;
    bool togglePatternAvailable = false;
    bool selectionItemPatternAvailable =
        false;
    bool expandCollapsePatternAvailable =
        false;
    bool valuePatternAvailable = false;
    BOOL valueReadOnly = FALSE;
    CONTROLTYPEID controlType = 0;
    BOOL enabled = FALSE;
    BOOL selected = FALSE;
    ToggleState toggleState =
        ToggleState_Indeterminate;
    ExpandCollapseState expansionState =
        ExpandCollapseState_LeafNode;
    std::wstring name;
    std::wstring help;
    std::wstring value;
};

class LfpScopedComApartment
{
public:
    LfpScopedComApartment()
        : result (
            CoInitializeEx (
                nullptr,
                COINIT_MULTITHREADED))
    {
    }

    ~LfpScopedComApartment()
    {
        if (SUCCEEDED (result))
            CoUninitialize();
    }

    HRESULT getResult() const
    {
        return result;
    }

private:
    const HRESULT result;
};

LfpWindowsUiaInvokeResult
invokeLfpWindowsUiaControl (
    HWND window,
    const std::wstring& automationId,
    LfpWindowsUiaAction action,
    const std::wstring& valueToSet = {})
{
    LfpWindowsUiaInvokeResult output;
    const LfpScopedComApartment
        comApartment;
    const auto comResult =
        comApartment.getResult();
    if (FAILED (comResult))
    {
        output.invokeResult =
            comResult;
        return output;
    }

    const auto finish =
        [&] (HRESULT result)
    {
        output.invokeResult = result;
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
        return finish (result);

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
        return finish (
            FAILED (result)
                ? result
                : E_FAIL);
    }

    VARIANT expectedId;
    VariantInit (&expectedId);
    expectedId.vt = VT_BSTR;
    expectedId.bstrVal =
        SysAllocString (
            automationId.c_str());
    if (expectedId.bstrVal
        == nullptr)
    {
        return finish (
            E_OUTOFMEMORY);
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
    VariantClear (&expectedId);
    if (FAILED (result))
        return finish (result);

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        element;
    result =
        rootElement
            ->FindFirst (
                TreeScope_Subtree,
                idCondition.Get(),
                &element);
    if (SUCCEEDED (result)
        && element == nullptr
        && automationId.find (
               L".choice.")
               != std::wstring::npos)
    {
        Microsoft::WRL::ComPtr<
            IUIAutomationElement>
            desktopElement;
        result =
            automation
                ->GetRootElement (
                    &desktopElement);
        if (SUCCEEDED (result)
            && desktopElement != nullptr)
        {
            result =
                desktopElement
                    ->FindFirst (
                        TreeScope_Subtree,
                        idCondition.Get(),
                        &element);
        }
    }
    if (FAILED (result)
        || element == nullptr)
    {
        return finish (
            FAILED (result)
                ? result
                : E_FAIL);
    }

    element->get_CurrentControlType (
        &output.controlType);
    element->get_CurrentIsEnabled (
        &output.enabled);
    BSTR text = nullptr;
    if (SUCCEEDED (
            element->get_CurrentName (
                &text))
        && text != nullptr)
    {
        output.name = text;
    }
    SysFreeString (text);
    text = nullptr;
    if (SUCCEEDED (
            element->get_CurrentHelpText (
                &text))
        && text != nullptr)
    {
        output.help = text;
    }
    SysFreeString (text);

    Microsoft::WRL::ComPtr<
        IUIAutomationInvokePattern>
        invokePattern;
    output.invokePatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_InvokePatternId,
                IID_PPV_ARGS (
                    &invokePattern));
    output.invokePatternAvailable =
        invokePattern != nullptr;

    Microsoft::WRL::ComPtr<
        IUIAutomationTogglePattern>
        togglePattern;
    output.togglePatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_TogglePatternId,
                IID_PPV_ARGS (
                    &togglePattern));
    output.togglePatternAvailable =
        togglePattern != nullptr;
    if (togglePattern != nullptr)
    {
        togglePattern
            ->get_CurrentToggleState (
                &output.toggleState);
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationSelectionItemPattern>
        selectionItemPattern;
    output.selectionItemPatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_SelectionItemPatternId,
                IID_PPV_ARGS (
                    &selectionItemPattern));
    output.selectionItemPatternAvailable =
        selectionItemPattern != nullptr;

    Microsoft::WRL::ComPtr<
        IUIAutomationExpandCollapsePattern>
        expandCollapsePattern;
    output.expandCollapsePatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_ExpandCollapsePatternId,
                IID_PPV_ARGS (
                    &expandCollapsePattern));
    output.expandCollapsePatternAvailable =
        expandCollapsePattern != nullptr;

    Microsoft::WRL::ComPtr<
        IUIAutomationValuePattern>
        valuePattern;
    output.valuePatternResult =
        element
            ->GetCurrentPatternAs (
                UIA_ValuePatternId,
                IID_PPV_ARGS (
                    &valuePattern));
    output.valuePatternAvailable =
        valuePattern != nullptr;
    if (valuePattern != nullptr)
    {
        valuePattern
            ->get_CurrentIsReadOnly (
                &output.valueReadOnly);
        BSTR value = nullptr;
        if (SUCCEEDED (
                valuePattern
                    ->get_CurrentValue (
                        &value))
            && value != nullptr)
        {
            output.value = value;
        }
        SysFreeString (value);
    }

    if (action
        == LfpWindowsUiaAction::
               queryToggle
        || action
               == LfpWindowsUiaAction::
                      toggle)
    {
        if (togglePattern == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        if (action
            == LfpWindowsUiaAction::
                   toggle)
        {
            result =
                togglePattern
                    ->Toggle();
        }

        if (SUCCEEDED (result))
        {
            result =
                togglePattern
                    ->get_CurrentToggleState (
                        &output.toggleState);
        }
        if (SUCCEEDED (result)
            && valuePattern != nullptr)
        {
            BSTR currentValue =
                nullptr;
            result =
                valuePattern
                    ->get_CurrentValue (
                        &currentValue);
            if (SUCCEEDED (result)
                && currentValue
                       != nullptr)
            {
                output.value =
                    currentValue;
            }
            SysFreeString (
                currentValue);
        }
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               queryValue)
    {
        return finish (
            valuePattern != nullptr
                ? S_OK
                : E_NOINTERFACE);
    }

    if (action
        == LfpWindowsUiaAction::
               queryInvoke)
    {
        return finish (
            invokePattern != nullptr
                ? S_OK
                : E_NOINTERFACE);
    }

    if (action
        == LfpWindowsUiaAction::
               setValue)
    {
        if (valuePattern == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        auto input =
            SysAllocString (
                valueToSet.c_str());
        if (input == nullptr)
        {
            return finish (
                E_OUTOFMEMORY);
        }
        result =
            valuePattern
                ->SetValue (
                    input);
        SysFreeString (
            input);
        if (FAILED (result))
            return finish (result);

        BSTR currentValue =
            nullptr;
        result =
            valuePattern
                ->get_CurrentValue (
                    &currentValue);
        if (SUCCEEDED (result)
            && currentValue
                   != nullptr)
        {
            output.value =
                currentValue;
        }
        SysFreeString (
            currentValue);
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               queryExpansion
        || action
               == LfpWindowsUiaAction::
                      expand
        || action
               == LfpWindowsUiaAction::
                      collapse)
    {
        if (expandCollapsePattern
            == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        if (action
            == LfpWindowsUiaAction::
                   expand)
        {
            output.focusResult =
                element->SetFocus();
            if (FAILED (output.focusResult))
                return finish (output.focusResult);
            result =
                expandCollapsePattern
                    ->Expand();
        }
        else if (action
                 == LfpWindowsUiaAction::
                        collapse)
        {
            result =
                expandCollapsePattern
                    ->Collapse();
        }

        if (SUCCEEDED (result))
        {
            result =
                expandCollapsePattern
                    ->get_CurrentExpandCollapseState (
                        &output
                             .expansionState);
        }
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               querySelection)
    {
        if (selectionItemPattern
            == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        result =
            selectionItemPattern
                ->get_CurrentIsSelected (
                    &output.selected);
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               select)
    {
        if (selectionItemPattern
            == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        result =
            selectionItemPattern
                ->Select();
        if (SUCCEEDED (result))
        {
            result =
                selectionItemPattern
                    ->get_CurrentIsSelected (
                        &output.selected);
        }
        return finish (result);
    }

    if (action
        == LfpWindowsUiaAction::
               removeFromSelection)
    {
        if (selectionItemPattern
            == nullptr)
        {
            return finish (
                E_NOINTERFACE);
        }

        result =
            selectionItemPattern
                ->RemoveFromSelection();
        if (SUCCEEDED (result))
        {
            result =
                selectionItemPattern
                    ->get_CurrentIsSelected (
                        &output.selected);
        }
        if (SUCCEEDED (result)
            && valuePattern != nullptr)
        {
            BSTR currentValue =
                nullptr;
            result =
                valuePattern
                    ->get_CurrentValue (
                        &currentValue);
            if (SUCCEEDED (result)
                && currentValue
                       != nullptr)
            {
                output.value =
                    currentValue;
            }
            SysFreeString (
                currentValue);
        }
        return finish (result);
    }

    result =
        output.invokePatternResult;
    if (FAILED (result)
        || invokePattern == nullptr)
    {
        return finish (
            FAILED (result)
                ? result
                : E_NOINTERFACE);
    }

    return finish (
        invokePattern->Invoke());
}

class LfpRetainedWindowsInvokeSession final
{
public:
    LfpRetainedWindowsInvokeSession (
        HWND windowToUse,
        std::wstring automationIdToUse)
        : window (windowToUse),
          automationId (
              std::move (
                  automationIdToUse)),
          worker (
              [this]
              {
                  run();
              })
    {
    }

    ~LfpRetainedWindowsInvokeSession()
    {
        command.store (stopCommand);
        if (worker.joinable())
            worker.join();
    }

    bool isReady() const
    {
        return ready.load();
    }

    HRESULT getSetupResult() const
    {
        return setupResult.load();
    }

    void requestInvoke()
    {
        invokeCompleted.store (false);
        command.store (invokeCommand);
    }

    bool hasInvokeCompleted() const
    {
        return invokeCompleted.load();
    }

    HRESULT getInvokeResult() const
    {
        return invokeResult.load();
    }

private:
    void run()
    {
        const LfpScopedComApartment
            comApartment;
        auto result =
            comApartment.getResult();
        Microsoft::WRL::ComPtr<
            IUIAutomation>
            automation;
        Microsoft::WRL::ComPtr<
            IUIAutomationElement>
            rootElement;
        Microsoft::WRL::ComPtr<
            IUIAutomationCondition>
            idCondition;
        Microsoft::WRL::ComPtr<
            IUIAutomationElement>
            element;
        Microsoft::WRL::ComPtr<
            IUIAutomationInvokePattern>
            invokePattern;

        if (SUCCEEDED (result))
        {
            result = CoCreateInstance (
                CLSID_CUIAutomation,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS (
                    &automation));
        }
        if (SUCCEEDED (result))
        {
            result = automation
                         ->ElementFromHandle (
                             window,
                             &rootElement);
            if (SUCCEEDED (result)
                && rootElement == nullptr)
            {
                result = E_FAIL;
            }
        }
        if (SUCCEEDED (result))
        {
            VARIANT expectedId;
            VariantInit (&expectedId);
            expectedId.vt = VT_BSTR;
            expectedId.bstrVal =
                SysAllocString (
                    automationId.c_str());
            if (expectedId.bstrVal == nullptr)
            {
                result = E_OUTOFMEMORY;
            }
            else
            {
                result = automation
                             ->CreatePropertyCondition (
                                 UIA_AutomationIdPropertyId,
                                 expectedId,
                                 &idCondition);
            }
            VariantClear (&expectedId);
        }
        if (SUCCEEDED (result))
        {
            result = rootElement
                         ->FindFirst (
                             TreeScope_Subtree,
                             idCondition.Get(),
                             &element);
            if (SUCCEEDED (result)
                && element == nullptr)
            {
                result = E_FAIL;
            }
        }
        if (SUCCEEDED (result))
        {
            result = element
                         ->GetCurrentPatternAs (
                             UIA_InvokePatternId,
                             IID_PPV_ARGS (
                                 &invokePattern));
            if (SUCCEEDED (result)
                && invokePattern == nullptr)
            {
                result = E_NOINTERFACE;
            }
        }

        setupResult.store (result);
        ready.store (true);

        while (true)
        {
            const auto requested =
                command.exchange (
                    noCommand);
            if (requested == stopCommand)
                return;
            if (requested == invokeCommand)
            {
                invokeResult.store (
                    invokePattern != nullptr
                        ? invokePattern->Invoke()
                        : result);
                invokeCompleted.store (true);
            }
            std::this_thread::yield();
        }
    }

    static constexpr int noCommand = 0;
    static constexpr int invokeCommand = 1;
    static constexpr int stopCommand = 2;

    const HWND window;
    const std::wstring automationId;
    std::atomic<bool> ready { false };
    std::atomic<bool>
        invokeCompleted { false };
    std::atomic<HRESULT>
        setupResult { E_PENDING };
    std::atomic<HRESULT>
        invokeResult { E_PENDING };
    std::atomic<int> command { noCommand };
    std::thread worker;
};
#endif
} // namespace

/*
    Class that can hold buffers of sample data written to the canvas and produce an 'expected' result image
     
    Used to verify the LFP Canvas' ability to accurately display voltages
 */
class ExpectedImage
{
public:
    ExpectedImage (int channels, int samples) : numChannels (channels), numSamplesInView (samples), lastSampleWritten (0)
    {
        for (int c = 0; c < numChannels; c++)
        {
            Array<float> newBuffer;
            newBuffer.insertMultiple (0, 0, samples);
            buffers.add (newBuffer);
        }
    }

    /*Adds samples from an AudioBuffer to internal buffer. Should be same buffer passed to the LFP Canvas*/
    void addToBuffer (AudioBuffer<float> newSamples)
    {
        for (int channelIndex = 0; channelIndex < numChannels; channelIndex++)
        {
            const float* channelReadPointer = newSamples.getReadPointer (channelIndex);
            int currentSampleBufferIndex = lastSampleWritten;
            for (int sampleIndex = 0; sampleIndex < newSamples.getNumSamples(); sampleIndex++)
            {
                float val = channelReadPointer[sampleIndex];
                Array<float>& writeBuffer = buffers.getReference (channelIndex);
                writeBuffer.set (currentSampleBufferIndex++, val);
                if (currentSampleBufferIndex >= numSamplesInView)
                {
                    currentSampleBufferIndex = 0;
                }
            }
        }
        lastSampleWritten += newSamples.getNumSamples() % numSamplesInView;
    }

    /*Constructs the trace information from internal buffers into an Image object*/
    Image getImage (int width, int height, Array<Colour> channelColours, Colour backgroundColour, Colour midlineColour, int scaleFactor) const
    {
        Image expectedImage (Image::ARGB, width, height, true, SoftwareImageType());

        //Fill image with background colour
        Graphics g (expectedImage);
        g.fillAll (backgroundColour);
        int heightPerChannel = height / numChannels;
        float samplesPerPixel = (float) numSamplesInView / (float) width;

        //Draw Midline
        for (int channelIndex = 0; channelIndex < numChannels; channelIndex += 1)
        {
            for (int x = 0; x < width; x++)
            {
                expectedImage.setPixelAt (x, channelIndex * heightPerChannel + heightPerChannel / 2, midlineColour);
            }
        }

        //Draw trace data
        for (int channelIndex = 0; channelIndex < numChannels; channelIndex++)
        {
            int lastPixelY = 0;
            for (int xWritePixel = 0; xWritePixel < width; xWritePixel++)
            {
                int avg = 0;
                int startSampleIndex = std::ceil (xWritePixel * samplesPerPixel);
                int endSampleIndex = (std::ceil ((xWritePixel + 1) * samplesPerPixel) - 1);
                for (int i = startSampleIndex; i < endSampleIndex; i++)
                {
                    avg += buffers[channelIndex][i];
                }

                avg /= (endSampleIndex - startSampleIndex);

                int amplitude = float (avg) / float (scaleFactor) * ((height / numChannels) / 2);
                int channelMidline = channelIndex * heightPerChannel + heightPerChannel / 2;
                int yWritePixel = channelMidline - amplitude;
                if (yWritePixel >= 0 && yWritePixel < height)
                {
                    expectedImage.setPixelAt (xWritePixel, yWritePixel, channelColours[channelIndex]);
                    if (xWritePixel != 0)
                    {
                        //Need to fill in gaps if  difference between y pixels
                        int currentY = lastPixelY;
                        while (currentY != yWritePixel)
                        {
                            expectedImage.setPixelAt (xWritePixel, currentY, channelColours[channelIndex]);
                            currentY = yWritePixel > currentY ? currentY + 1 : currentY - 1;
                        }
                    }
                    lastPixelY = yWritePixel;
                }
            }
        }

        //Draw playback line
        int playbackXPixel = int (std::ceil (float (lastSampleWritten) / float (samplesPerPixel))) % width + 1;
        for (int playbackYPixel = 0; playbackYPixel < height; playbackYPixel += 2)
        {
            expectedImage.setPixelAt (playbackXPixel, playbackYPixel + 1, Colours::yellow);
        }

        return expectedImage;
    }

    int lastSampleWritten;

    int numSamplesInView;
    int numChannels;
    Array<Array<float>> buffers;

    int height;
    int width;
};

class LfpDisplayNodeTests : public testing::Test
{
protected:
    void SetUp() override
    {
        numChannels = 16;
        tester =
            std::make_unique<
                ProcessorTester> (
                TestSourceNodeBuilder (
                    FakeSourceNodeParams {
                        numChannels,
                        sampleRate,
                        bitVolts }),
                TestGuiRuntimeLifetime::
                    process);

        processor = tester->createProcessor<LfpViewer::LfpDisplayNode> (Plugin::Processor::SINK);

        width = 0;
        height = 0;
        x = 0;
        y = 0;

        midlineColour = Colour (50, 50, 50);
        playheadColour = Colours::yellow;
    }

    /*Create a new AudioBuffer filled with step data*/
    AudioBuffer<float> createBuffer (float starting_value, float step, int numChannels, int numSamples)
    {
        AudioBuffer<float> inputBuffer (numChannels, numSamples);

        // in microvolts
        float currValue = starting_value;
        for (int chidx = 0; chidx < numChannels; chidx++)
        {
            for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++)
            {
                inputBuffer.setSample (chidx, sampleIdx, currValue);
                currValue += step;
            }
        }
        return inputBuffer;
    }

    std::unique_ptr<LfpViewer::LfpDisplayCanvas>
    createAveragingCanvas (LfpViewer::SplitLayouts layout = LfpViewer::SplitLayouts::SINGLE)
    {
        auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
            processor, layout, false);
        canvas->updateSettings();
        canvas->setSize (900, 600);
        canvas->resized();
        canvas->setVisible (true);
        return canvas;
    }

    std::vector<LfpViewer::LfpDisplaySplitter*>
    getDisplaySplitters (LfpViewer::LfpDisplayCanvas& canvas)
    {
        std::vector<LfpViewer::LfpDisplaySplitter*> splitters;
        collectLfpDescendants (canvas, splitters);
        std::sort (
            splitters.begin(), splitters.end(),
            [] (const auto* lhs, const auto* rhs)
            {
                return lhs->splitID < rhs->splitID;
            });
        return splitters;
    }

    UtilityButton* getResetTrialsButton (LfpViewer::LfpDisplaySplitter& splitter)
    {
        std::vector<UtilityButton*> buttons;
        collectLfpDescendants (*splitter.options, buttons);
        const auto reset = std::find_if (
            buttons.begin(), buttons.end(), [] (UtilityButton* candidate)
            {
                return candidate->getLabel() == "RESET";
            });
        return reset == buttons.end() ? nullptr : *reset;
    }

    void processConstantBlock (float value, int numSamples = 400)
    {
        auto block = createBuffer (value, 0.0f, numChannels, numSamples);
        tester->processBlock (processor, block);
        currentSampleIndex += numSamples;
    }

    void processTriggeredBlock (
        float value, int numSamples = 400, int triggerOffset = 200)
    {
        const auto streamId = processor->getDataStreams()[0]->getStreamId();
        const auto* sourceStream = tester->getSourceNodeDataStream (streamId);
        ASSERT_NE (sourceStream, nullptr);
        const auto eventChannels = sourceStream->getEventChannels();
        ASSERT_GT (eventChannels.size(), 0);
        auto event = TTLEvent::createTTLEvent (
            eventChannels[0],
            currentSampleIndex + triggerOffset,
            uint8 (0),
            true);
        auto block = createBuffer (value, 0.0f, numChannels, numSamples);
        tester->processBlock (processor, block, event.get());
        currentSampleIndex += numSamples;
    }

    void finishTriggeredView (
        LfpViewer::LfpDisplayCanvas& canvas, float value)
    {
        for (int block = 0; block < 10; ++block)
        {
            processConstantBlock (value);
            canvas.refreshState();
        }
    }

    void beginTriggeredAveraging (
        LfpViewer::LfpDisplayCanvas& canvas,
        const std::vector<LfpViewer::LfpDisplaySplitter*>& splitters)
    {
        for (auto* split : splitters)
        {
            split->options->setTriggerSourceSelection (2);
            split->options->setAveraging (true);
        }
        processor->startAcquisition();
        canvas.beginAnimation();
        canvas.endAnimation();
    }

    void startTrial (
        LfpViewer::LfpDisplayCanvas& canvas, float value)
    {
        processConstantBlock (value, 1024);
        processTriggeredBlock (value);
        canvas.refreshState();
    }

    bool invokeLfpActionFromWorker (
        const AccessibilityActions& actions,
        AccessibilityActionType action)
    {
        std::atomic<bool> workerReturned { false };
        bool invoked = false;
        std::thread worker (
            [&]
            {
                invoked = actions.invoke (action);
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return invoked;
    }

    void pumpUntilButtonCallbacks (
        const LfpThreadTrackingButtonListener& listener,
        int expectedCount)
    {
        for (int attempt = 0;
             attempt < 100
                 && listener.callbackCount.load()
                        < expectedCount;
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
    }

#if JUCE_WINDOWS
    LfpWindowsUiaInvokeResult
    invokeWindowsUiaFromWorker (
        HWND window,
        StringRef id,
        LfpWindowsUiaAction action)
    {
        LfpWindowsUiaInvokeResult result;
        std::atomic<bool> workerReturned { false };
        std::thread worker (
            [&]
            {
                result = invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (
                        String (id)
                            .toWideCharPointer()),
                    action);
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return result;
    }
#endif

    /*Creates a new AudioBuffer filled with sinusoidal waves*/
    AudioBuffer<float> createBufferSinusoidal (int cycles, int numChannels, int numSamples, int amplitude)
    {
        AudioBuffer<float> inputBuffer (numChannels, numSamples);
        float pi = 3.1415;
        // in microvolts
        for (int chidx = 0; chidx < numChannels; chidx++)
        {
            for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++)
            {
                float value = amplitude * sin (float (sampleIdx) / (float (numSamples) / float (cycles)) * 2 * pi);
                inputBuffer.setSample (chidx, sampleIdx, value);
            }
        }
        return inputBuffer;
    }

    /*Writes data from an AudioBuffer to processor. Verifies that the AudioBuffer is not changed*/
    void writeBlock (AudioBuffer<float>& inputBuffer)
    {
        auto outputBuffer = tester->processBlock (processor, inputBuffer);
        // Assert the buffer hasn't changed after process()
        ASSERT_EQ (inputBuffer.getNumSamples(), outputBuffer.getNumSamples());
        ASSERT_EQ (inputBuffer.getNumChannels(), outputBuffer.getNumChannels());
        for (int chidx = 0; chidx < inputBuffer.getNumChannels(); chidx++)
        {
            for (int sampleIdx = 0; sampleIdx < inputBuffer.getNumSamples(); ++sampleIdx)
            {
                ASSERT_EQ (inputBuffer.getSample (chidx, sampleIdx), outputBuffer.getSample (chidx, sampleIdx));
            }
        }
        currentSampleIndex += inputBuffer.getNumSamples();
    }

    /*Gets information from canvas needed to build an ExpectedImage*/
    void setExpectedImageParameters (LfpViewer::LfpDisplayCanvas* canvas)
    {
        canvas->getChannelBitmapBounds (0, x, y, width, height);
        canvas->getChannelColours (0, channelColours, backgroundColour);
        ASSERT_EQ (channelColours.size(), numChannels);
    }

    /*Compares 2 Image objects and returns the number of different pixels*/
    int getImageDifferencePixelCount (Image expectedImage, Image actualImage) const
    {
        int missCount = 0;
        Image diffImage (Image::ARGB, width, height, true);
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                Colour expectedPixel = expectedImage.getPixelAt (x, y);
                Colour actualPixel = actualImage.getPixelAt (x, y);
                if (expectedPixel != actualPixel)
                {
                    //Allow some variance - if expectedPixel is a voltage Pixel then see if actualPixel +- 1 pixel is also a voltage Pixel.
                    //If true then don't count as a miss
                    if (expectedPixel != backgroundColour && expectedPixel != midlineColour && expectedPixel != playheadColour)
                    {
                        Colour postiveVariance = actualImage.getPixelAt (x, y + 1);
                        Colour negativeVariance = actualImage.getPixelAt (x, y - 1);
                        if (postiveVariance == expectedPixel || negativeVariance == expectedPixel)
                        {
                            continue;
                        }
                    }
                    //Same for if actualPixel is a voltagePixel and expectedPixel isn't
                    if (actualPixel != backgroundColour && actualPixel != midlineColour && actualPixel != playheadColour)
                    {
                        Colour postiveVariance = expectedImage.getPixelAt (x, y + 1);
                        Colour negativeVariance = expectedImage.getPixelAt (x, y - 1);
                        if (postiveVariance == actualPixel || negativeVariance == actualPixel)
                        {
                            continue;
                        }
                    }
                    missCount += 1;
                }
            }
        }
        return missCount;
    }

    void dumpPng (String path, Image image)
    {
        FileOutputStream stream ((File (path)));
        PNGImageFormat pngWriter;
        pngWriter.writeImageToStream (image, stream);
    }

    LfpViewer::LfpDisplayEditor*
    createLayoutEditor()
    {
        processor->setHeadlessMode (
            false);
        processor->setProcessorType (
            Plugin::Processor::
                SPLITTER);
        auto* editor =
            static_cast<
                LfpViewer::
                    LfpDisplayEditor*> (
                processor
                    ->createEditor());
        processor->setProcessorType (
            Plugin::Processor::SINK);
        if (editor != nullptr)
        {
            editor->addToDesktop (0);
            editor->setVisible (true);
        }
        return editor;
    }

    LfpViewer::LfpDisplayNode* processor;

    int numChannels;
    float bitVolts = 1.0;
    std::unique_ptr<ProcessorTester> tester;
    int64_t currentSampleIndex = 0;
    float sampleRate = 2000;

    Image expectedImage;
    Colour midlineColour;
    Colour backgroundColour;
    Colour playheadColour;

    Array<Colour> channelColours;

    int width;
    int height;
    int x;
    int y;
};

TEST_F (LfpDisplayNodeTests,
        ExposesLayoutAndSyncControls)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp";
    struct ExpectedControl
    {
        String id;
        String title;
        String description;
        AccessibilityRole role;
        bool checked;
        bool enabled;
    };
    const std::array<
        ExpectedControl,
        6>
        expected {
            ExpectedControl {
                prefix
                    + ".layout.single",
                "Single display",
                "Show one LFP display pane.",
                AccessibilityRole::
                    radioButton,
                true,
                true },
            ExpectedControl {
                prefix
                    + ".layout.two_vertical",
                "Two vertical displays",
                "Show two LFP display panes side by side.",
                AccessibilityRole::
                    radioButton,
                false,
                true },
            ExpectedControl {
                prefix
                    + ".layout.three_vertical",
                "Three vertical displays",
                "Show three LFP display panes side by side.",
                AccessibilityRole::
                    radioButton,
                false,
                true },
            ExpectedControl {
                prefix
                    + ".layout.two_horizontal",
                "Two horizontal displays",
                "Show two stacked LFP display panes.",
                AccessibilityRole::
                    radioButton,
                false,
                true },
            ExpectedControl {
                prefix
                    + ".layout.three_horizontal",
                "Three horizontal displays",
                "Show three stacked LFP display panes.",
                AccessibilityRole::
                    radioButton,
                false,
                true },
            ExpectedControl {
                prefix
                    + ".sync_displays",
                "Sync displays",
                "Realign every LFP display pane with the latest data buffer position.",
                AccessibilityRole::button,
                false,
                false } };

    for (const auto& control :
         expected)
    {
        auto* component =
            findLfpDescendantById (
                *editor,
                control.id);
        ASSERT_NE (
            component,
            nullptr)
            << control.id;
        auto* handler =
            component
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getTitle(),
            control.title);
        EXPECT_EQ (
            handler->getRole(),
            control.role);
        EXPECT_EQ (
            handler->getDescription(),
            control.description);
        EXPECT_EQ (
            handler->getHelp(),
            control.description);
        EXPECT_EQ (
            handler->isEnabled(),
            control.enabled);
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        if (control.role
            == AccessibilityRole::
                   radioButton)
        {
            EXPECT_FALSE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            toggle));
            EXPECT_EQ (
                handler
                    ->getCurrentState()
                    .isChecked(),
                control.checked);
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        LayoutSelectionRunsFromWorker)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.two_vertical";
    auto* component =
        findLfpDescendantById (
            *editor,
            id);
    ASSERT_NE (component, nullptr);
    auto* button =
        dynamic_cast<Button*> (
            component);
    ASSERT_NE (button, nullptr);
    auto* handler =
        component
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();

    std::atomic<bool>
        workerReturned { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 50
             && (! workerReturned.load()
                 || ! button
                           ->getToggleState());
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    worker.join();

    EXPECT_TRUE (invoked);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isChecked());

    for (const auto* suffix :
         { "single",
           "two_vertical",
           "three_vertical",
           "two_horizontal",
           "three_horizontal" })
    {
        auto* layout =
            findLfpDescendantById (
                *editor,
                "oe.processor."
                    + String (
                        processor->getNodeId())
                    + ".lfp.layout."
                    + suffix);
        ASSERT_NE (layout, nullptr);
        EXPECT_EQ (
            layout
                ->getAccessibilityHandler()
                ->getCurrentState()
                .isChecked(),
            String (suffix)
                == "two_vertical");
    }
}

TEST_F (LfpDisplayNodeTests,
        LayoutProvidersReadFromWorker)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.single";
    auto* component =
        findLfpDescendantById (
            *editor,
            id);
    ASSERT_NE (component, nullptr);
    auto* handler =
        component
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);

    String title;
    String description;
    String help;
    bool enabled = false;
    bool checkable = false;
    bool checked = false;
    std::thread worker (
        [&]
        {
            title = handler->getTitle();
            description =
                handler->getDescription();
            help = handler->getHelp();
            enabled =
                handler->isEnabled();
            const auto state =
                handler->getCurrentState();
            checkable =
                state.isCheckable();
            checked =
                state.isChecked();
        });
    worker.join();

    EXPECT_EQ (
        title,
        "Single display");
    EXPECT_EQ (
        description,
        "Show one LFP display pane.");
    EXPECT_EQ (help, description);
    EXPECT_TRUE (enabled);
    EXPECT_FALSE (checkable);
    EXPECT_TRUE (checked);
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerInvokesAndSelectsLayoutRadioButton)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);
    editor->setBounds (
        0,
        0,
        195,
        120);
    editor->setAlwaysOnTop (
        true);
    editor->toFront (false);
    ASSERT_TRUE (
        editor->isShowing());

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.two_vertical";
    auto* component =
        findLfpDescendantById (
            *editor,
            id);
    ASSERT_NE (component, nullptr);
    auto* button =
        dynamic_cast<Button*> (
            component);
    ASSERT_NE (button, nullptr);
    const std::wstring wideId (
        id.toWideCharPointer());
    const auto window =
        static_cast<HWND> (
            editor
                ->getWindowHandle());
    ASSERT_NE (window, nullptr);

    LfpWindowsUiaInvokeResult
        result;
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            result =
                invokeLfpWindowsUiaControl (
                    window,
                    wideId,
                    LfpWindowsUiaAction::
                        invoke);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || ! button
                           ->getToggleState());
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    worker.join();

    EXPECT_EQ (
        result.invokeResult,
        S_OK);
    EXPECT_NE (
        result.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
    EXPECT_FALSE (
        result
            .togglePatternAvailable);
    EXPECT_EQ (
        result
            .selectionItemPatternResult,
        S_OK);
    EXPECT_TRUE (
        result
            .selectionItemPatternAvailable);
    EXPECT_EQ (
        result.controlType,
        UIA_RadioButtonControlTypeId);
    EXPECT_EQ (
        result.enabled,
        TRUE);
    EXPECT_EQ (
        result.name,
        L"Two vertical displays");
    EXPECT_EQ (
        result.help,
        L"Show two LFP display panes side by side.");
    EXPECT_TRUE (
        button->getToggleState());

    const auto selectedId =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.three_horizontal";
    auto* selectedButton =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *editor,
                selectedId));
    ASSERT_NE (
        selectedButton,
        nullptr);
    const std::wstring
        wideSelectedId (
            selectedId
                .toWideCharPointer());
    LfpWindowsUiaInvokeResult
        selectedResult;
    workerReturned.store (
        false);
    std::thread selectionWorker (
        [&]
        {
            selectedResult =
                invokeLfpWindowsUiaControl (
                    window,
                    wideSelectedId,
                    LfpWindowsUiaAction::
                        select);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || ! selectedButton
                           ->getToggleState());
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    selectionWorker.join();

    EXPECT_EQ (
        selectedResult
            .invokeResult,
        S_OK);
    EXPECT_TRUE (
        selectedButton
            ->getToggleState());
    EXPECT_FALSE (
        button->getToggleState());
    for (const auto* suffix :
         { "single",
           "two_vertical",
           "three_vertical",
           "two_horizontal",
           "three_horizontal" })
    {
        auto* layout =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *editor,
                    "oe.processor."
                        + String (
                            processor
                                ->getNodeId())
                        + ".lfp.layout."
                        + suffix));
        ASSERT_NE (layout, nullptr);
        EXPECT_EQ (
            layout
                ->getToggleState(),
            String (suffix)
                == "three_horizontal");
    }

    LfpWindowsUiaInvokeResult
        queryResult;
    workerReturned.store (
        false);
    std::thread queryWorker (
        [&]
        {
            queryResult =
                invokeLfpWindowsUiaControl (
                    window,
                    wideSelectedId,
                    LfpWindowsUiaAction::
                        querySelection);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    queryWorker.join();
    EXPECT_EQ (
        queryResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        queryResult.selected,
        TRUE);
}
#endif

TEST_F (LfpDisplayNodeTests,
        QueuedLayoutPressHonoursLatestAvailability)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.";
    auto* component =
        findLfpDescendantById (
            *editor,
            prefix
                + "two_vertical");
    ASSERT_NE (component, nullptr);
    auto* button =
        dynamic_cast<Button*> (
            component);
    ASSERT_NE (button, nullptr);
    auto* handler =
        component
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto actions =
        handler->getActions();

    std::atomic<bool>
        workerReturned { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    while (! workerReturned.load())
        std::this_thread::yield();
    worker.join();

    button->setEnabled (false);
    EXPECT_FALSE (
        handler->isEnabled());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_TRUE (invoked);
    EXPECT_FALSE (
        button->getToggleState());

    invoked = false;
    workerReturned.store (
        false);
    std::thread disabledWorker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    while (! workerReturned.load())
        std::this_thread::yield();
    disabledWorker.join();
    button->setEnabled (true);
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_TRUE (invoked);
    EXPECT_FALSE (
        button->getToggleState());

    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                press));
    for (int attempt = 0;
         attempt < 50
             && ! button
                     ->getToggleState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    EXPECT_TRUE (
        button->getToggleState());
}

TEST_F (LfpDisplayNodeTests,
        StaleLayoutActionDoesNotAffectReplacementEditor)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.layout.";
    auto* oldControl =
        findLfpDescendantById (
            *editor,
            prefix
                + "two_vertical");
    ASSERT_NE (oldControl, nullptr);
    const auto staleActions =
        oldControl
            ->getAccessibilityHandler()
            ->getActions();

    auto* replacement =
        createLayoutEditor();
    ASSERT_NE (replacement, nullptr);
    auto* replacementSingle =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *replacement,
                prefix
                    + "single"));
    auto* replacementTwoVertical =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *replacement,
                prefix
                    + "two_vertical"));
    ASSERT_NE (
        replacementSingle,
        nullptr);
    ASSERT_NE (
        replacementTwoVertical,
        nullptr);

    std::atomic<bool>
        workerReturned { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked =
                staleActions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 50
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    worker.join();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);

    EXPECT_TRUE (invoked);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_TRUE (
        replacementSingle
            ->getToggleState());
    EXPECT_FALSE (
        replacementTwoVertical
            ->getToggleState());
}

TEST_F (LfpDisplayNodeTests,
        SyncDisplayRunsOnMessageThreadFromWorker)
{
    auto* editor =
        createLayoutEditor();
    ASSERT_NE (editor, nullptr);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.sync_displays";
    auto* component =
        findLfpDescendantById (
            *editor,
            id);
    ASSERT_NE (component, nullptr);
    auto* button =
        dynamic_cast<Button*> (
            component);
    ASSERT_NE (button, nullptr);
    auto* handler =
        component
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_FALSE (
        handler->isEnabled());

    LfpThreadTrackingButtonListener
        listener;
    button->addListener (&listener);
    const auto actions =
        handler->getActions();
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                press));

    processor->setHeadlessMode (
        true);
    editor->canvas.reset (
        editor->createNewCanvas());
    ASSERT_NE (
        editor->canvas,
        nullptr);
    EXPECT_TRUE (
        handler->isEnabled());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);

    std::atomic<bool>
        workerReturned { false };
    bool invoked = false;
    std::thread worker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 50
             && (! workerReturned.load()
                 || listener
                            .callbackCount
                            .load()
                        == 0);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    worker.join();
    button->removeListener (
        &listener);

    EXPECT_TRUE (invoked);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());

    editor->canvas.reset();
    processor->setHeadlessMode (
        false);
}

TEST_F (LfpDisplayNodeTests,
        ExposesPaneSelectorForEveryDisplay)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto help =
        "Select LFP display %d as the active pane. In multi-pane layouts, the active pane shows its options and receives Space-bar pause commands. This changes display focus only; acquisition and recording are unaffected.";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".pane_selector";
        auto* selector =
            findLfpDescendantById (
                *canvas,
                id);
        ASSERT_NE (
            selector,
            nullptr)
            << id;
        auto* timescale =
            dynamic_cast<
                LfpViewer::
                    LfpTimescale*> (
                selector);
        ASSERT_NE (
            timescale,
            nullptr);
        auto* handler =
            timescale
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                radioButton);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " selector");
        EXPECT_EQ (
            handler->getDescription(),
            String::formatted (
                help,
                displayNumber));
        EXPECT_EQ (
            handler->getHelp(),
            String::formatted (
                help,
                displayNumber));
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            displayIndex == 0
                ? "Selected"
                : "Not selected");
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        EXPECT_EQ (
            timescale->isShowing(),
            displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        PaneSelectorWorkerSelectsOnePaneAndRoutesOptionsAndSpace)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::
                    THREE_HORZ,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (
        splitters.size(),
        3);

    EXPECT_TRUE (
        canvas->isPaneActive (0));
    EXPECT_FALSE (
        canvas->isPaneActive (1));
    EXPECT_FALSE (
        canvas->isPaneActive (2));
    EXPECT_TRUE (
        splitters[0]
            ->options
            ->isVisible());
    EXPECT_FALSE (
        splitters[1]
            ->options
            ->isVisible());
    EXPECT_FALSE (
        splitters[2]
            ->options
            ->isVisible());

    auto* handler =
        splitters[1]
            ->timescale
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    const auto actions =
        handler->getActions();
    EXPECT_TRUE (
        invokeLfpActionFromWorker (
            actions,
            AccessibilityActionType::
                press));

    EXPECT_FALSE (
        canvas->isPaneActive (0));
    EXPECT_TRUE (
        canvas->isPaneActive (1));
    EXPECT_FALSE (
        canvas->isPaneActive (2));
    EXPECT_FALSE (
        splitters[0]
            ->getSelectedState());
    EXPECT_TRUE (
        splitters[1]
            ->getSelectedState());
    EXPECT_FALSE (
        splitters[2]
            ->getSelectedState());
    EXPECT_FALSE (
        splitters[0]
            ->options
            ->isVisible());
    EXPECT_TRUE (
        splitters[1]
            ->options
            ->isVisible());
    EXPECT_FALSE (
        splitters[2]
            ->options
            ->isVisible());
    EXPECT_EQ (
        splitters[0]
            ->timescale
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Not selected");
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Selected");

    EXPECT_TRUE (
        canvas->keyPressed (
            KeyPress (
                KeyPress::spaceKey,
                ModifierKeys(),
                ' ')));
    EXPECT_FALSE (
        splitters[0]
            ->lfpDisplay
            ->isPaused());
    EXPECT_TRUE (
        splitters[1]
            ->lfpDisplay
            ->isPaused());
    EXPECT_FALSE (
        splitters[2]
            ->lfpDisplay
            ->isPaused());

    EXPECT_TRUE (
        invokeLfpActionFromWorker (
            actions,
            AccessibilityActionType::
                press));
    EXPECT_TRUE (
        canvas->isPaneActive (1));
}

TEST_F (LfpDisplayNodeTests,
        PaneSelectorTracksSingleAndHiddenPaneLayoutTransitions)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::
                    THREE_VERT,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (
        splitters.size(),
        3);
    ASSERT_TRUE (
        invokeLfpActionFromWorker (
            splitters[1]
                ->timescale
                ->getAccessibilityHandler()
                ->getActions(),
            AccessibilityActionType::
                press));
    ASSERT_TRUE (
        splitters[1]
            ->getSelectedState());

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::SINGLE);
    EXPECT_TRUE (
        canvas->isPaneActive (0));
    EXPECT_FALSE (
        canvas->isPaneActive (1));
    EXPECT_FALSE (
        canvas->isPaneActive (2));
    EXPECT_EQ (
        splitters[0]
            ->timescale
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Selected");
    EXPECT_EQ (
        splitters[1]
            ->timescale
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Not selected");
    EXPECT_TRUE (
        splitters[1]
            ->getSelectedState());

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    EXPECT_FALSE (
        canvas->isPaneActive (0));
    EXPECT_TRUE (
        canvas->isPaneActive (1));
    EXPECT_FALSE (
        canvas->isPaneActive (2));

    ASSERT_TRUE (
        invokeLfpActionFromWorker (
            splitters[2]
                ->timescale
                ->getAccessibilityHandler()
                ->getActions(),
            AccessibilityActionType::
                press));
    ASSERT_TRUE (
        canvas->isPaneActive (2));
    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                TWO_HORZ);
    EXPECT_TRUE (
        canvas->isPaneActive (0));
    EXPECT_FALSE (
        canvas->isPaneActive (1));
    EXPECT_FALSE (
        canvas->isPaneActive (2));
    EXPECT_FALSE (
        splitters[2]
            ->isVisible());
}

TEST_F (LfpDisplayNodeTests,
        PaneSelectorMatchesTimescaleWaveformStreamAndZeroChannelPaths)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::
                    THREE_HORZ,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (
        splitters.size(),
        3);

    const auto mouseDown =
        [] (Component& component)
    {
        const auto now =
            Time::getCurrentTime();
        MouseEvent event (
            Desktop::getInstance()
                .getMainMouseSource(),
            Point<float> (10.0f, 10.0f),
            ModifierKeys (
                ModifierKeys::
                    leftButtonModifier),
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &component,
            &component,
            now,
            Point<float> (10.0f, 10.0f),
            now,
            1,
            false);
        component.mouseDown (
            event);
    };

    mouseDown (
        *splitters[1]
             ->timescale);
    EXPECT_TRUE (
        canvas->isPaneActive (1));

    mouseDown (
        *splitters[0]
             ->lfpDisplay);
    EXPECT_TRUE (
        canvas->isPaneActive (0));

    auto* stream =
        splitters[2]
            ->streamSelection
            .get();
    ASSERT_NE (
        stream,
        nullptr);
    const auto streamId =
        stream->getItemId (0);
    ASSERT_GT (
        streamId,
        0);
    stream->setSelectedId (
        0,
        dontSendNotification);
    stream->setSelectedId (
        streamId,
        sendNotification);
    for (int attempt = 0;
         attempt < 100
             && ! canvas
                       ->isPaneActive (2);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_TRUE (
        canvas->isPaneActive (2));

    auto zeroChannelTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto zeroCanvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::
                SplitLayouts::
                    TWO_HORZ,
            false);
    zeroCanvas->updateSettings();
    zeroCanvas->setSize (
        900,
        800);
    zeroCanvas->addToDesktop (0);
    zeroCanvas->setVisible (true);
    const auto zeroSplitters =
        getDisplaySplitters (
            *zeroCanvas);
    ASSERT_EQ (
        zeroSplitters.size(),
        3);
    EXPECT_EQ (
        zeroChannelProcessor
            ->getNumInputs(),
        0);
    EXPECT_TRUE (
        invokeLfpActionFromWorker (
            zeroSplitters[1]
                ->timescale
                ->getAccessibilityHandler()
                ->getActions(),
            AccessibilityActionType::
                press));
    EXPECT_TRUE (
        zeroCanvas
            ->isPaneActive (1));
}

TEST_F (LfpDisplayNodeTests,
        PaneSelectorRetainedActionsHonourLatestAvailabilityAndDestruction)
{
    AccessibilityActions
        retainedActions;
    {
        auto canvas =
            std::make_unique<
                LfpViewer::
                    LfpDisplayCanvas> (
                processor,
                LfpViewer::
                    SplitLayouts::
                        TWO_VERT,
                false);
        canvas->updateSettings();
        canvas->setSize (900, 800);
        canvas->addToDesktop (0);
        canvas->setVisible (true);
        const auto splitters =
            getDisplaySplitters (
                *canvas);
        ASSERT_EQ (
            splitters.size(),
            3);
        auto* handler =
            splitters[1]
                ->timescale
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        retainedActions =
            handler->getActions();

        std::atomic<bool>
            workerReturned { false };
        bool invoked = false;
        std::thread worker (
            [&]
            {
                invoked =
                    retainedActions.invoke (
                        AccessibilityActionType::
                            press);
                workerReturned.store (
                    true);
            });
        splitters[1]
            ->timescale
            ->setEnabled (
                false);
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        EXPECT_TRUE (invoked);
        EXPECT_TRUE (
            canvas->isPaneActive (0));

        splitters[1]
            ->timescale
            ->setEnabled (
                true);
        const auto originalBounds =
            splitters[1]
                ->timescale
                ->getBounds();
        splitters[1]
            ->timescale
            ->setBounds (
                -originalBounds
                     .getWidth()
                    - 10,
                originalBounds
                    .getY(),
                originalBounds
                    .getWidth(),
                originalBounds
                    .getHeight());
        EXPECT_TRUE (
            invokeLfpActionFromWorker (
                retainedActions,
                AccessibilityActionType::
                    press));
        EXPECT_TRUE (
            canvas->isPaneActive (0));
        splitters[1]
            ->timescale
            ->setBounds (
                originalBounds);

        canvas->setLayout (
            LfpViewer::
                SplitLayouts::SINGLE);
        EXPECT_TRUE (
            invokeLfpActionFromWorker (
                retainedActions,
                AccessibilityActionType::
                    press));
        EXPECT_TRUE (
            canvas->isPaneActive (0));
        EXPECT_FALSE (
            canvas->isPaneActive (1));
    }

    EXPECT_TRUE (
        invokeLfpActionFromWorker (
            retainedActions,
            AccessibilityActionType::
                press));

    auto replacement =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::
                    TWO_VERT,
            false);
    replacement->updateSettings();
    replacement->setSize (
        900,
        800);
    replacement->addToDesktop (0);
    replacement->setVisible (true);
    ASSERT_TRUE (
        replacement
            ->isPaneActive (0));
    EXPECT_TRUE (
        invokeLfpActionFromWorker (
            retainedActions,
            AccessibilityActionType::
                press));
    EXPECT_TRUE (
        replacement
            ->isPaneActive (0));
    EXPECT_FALSE (
        replacement
            ->isPaneActive (1));
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaSelectsPaneWithoutHidingExistingPaneControls)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::
                    THREE_HORZ,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 900);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->setAlwaysOnTop (
        true);
    canvas->toFront (false);
    ASSERT_TRUE (
        canvas->isShowing());
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (
        splitters.size(),
        3);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto firstId =
        prefix
        + "1.pane_selector";
    const auto secondId =
        prefix
        + "2.pane_selector";
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto initialResult =
        invokeWindowsUiaFromWorker (
            window,
            firstId,
            LfpWindowsUiaAction::
                querySelection);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_RadioButtonControlTypeId);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 selector");
    EXPECT_EQ (
        initialResult.help,
        L"Select LFP display 1 as the active pane. In multi-pane layouts, the active pane shows its options and receives Space-bar pause commands. This changes display focus only; acquisition and recording are unaffected.");
    EXPECT_TRUE (
        initialResult
            .selectionItemPatternAvailable);
    EXPECT_TRUE (
        initialResult
            .invokePatternAvailable);
    EXPECT_FALSE (
        initialResult
            .togglePatternAvailable);
    EXPECT_TRUE (
        initialResult
            .valuePatternAvailable);
    EXPECT_EQ (
        initialResult
            .valueReadOnly,
        TRUE);
    EXPECT_EQ (
        initialResult.value,
        L"Selected");
    EXPECT_EQ (
        initialResult.selected,
        TRUE);

    const auto selectResult =
        invokeWindowsUiaFromWorker (
            window,
            secondId,
            LfpWindowsUiaAction::
                select);
    EXPECT_EQ (
        selectResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        selectResult.selected,
        TRUE);
    EXPECT_TRUE (
        canvas->isPaneActive (1));
    EXPECT_TRUE (
        splitters[1]
            ->options
            ->isVisible());

    const auto removeSelectedResult =
        invokeWindowsUiaFromWorker (
            window,
            secondId,
            LfpWindowsUiaAction::
                removeFromSelection);
    EXPECT_EQ (
        removeSelectedResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        removeSelectedResult.selected,
        TRUE);
    EXPECT_EQ (
        removeSelectedResult.value,
        L"Selected");
    EXPECT_TRUE (
        canvas->isPaneActive (1));
    EXPECT_TRUE (
        splitters[1]
            ->options
            ->isVisible());

    const auto removeUnselectedResult =
        invokeWindowsUiaFromWorker (
            window,
            firstId,
            LfpWindowsUiaAction::
                removeFromSelection);
    EXPECT_EQ (
        removeUnselectedResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        removeUnselectedResult
            .selected,
        FALSE);
    EXPECT_EQ (
        removeUnselectedResult.value,
        L"Not selected");
    EXPECT_TRUE (
        canvas->isPaneActive (1));
    EXPECT_FALSE (
        canvas->isPaneActive (0));
    EXPECT_TRUE (
        splitters[1]
            ->options
            ->isVisible());

    const auto secondValue =
        invokeWindowsUiaFromWorker (
            window,
            secondId,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        secondValue.invokeResult,
        S_OK);
    EXPECT_EQ (
        secondValue.value,
        L"Selected");
    const auto firstSelection =
        invokeWindowsUiaFromWorker (
            window,
            firstId,
            LfpWindowsUiaAction::
                querySelection);
    EXPECT_EQ (
        firstSelection.invokeResult,
        S_OK);
    EXPECT_EQ (
        firstSelection.selected,
        FALSE);

    const auto streamResult =
        invokeWindowsUiaFromWorker (
            window,
            prefix
                + "2.stream",
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        streamResult.invokeResult,
        S_OK);
    const auto timebaseResult =
        invokeWindowsUiaFromWorker (
            window,
            prefix
                + "2.timebase",
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        timebaseResult.invokeResult,
        S_OK);
    const auto pauseResult =
        invokeWindowsUiaFromWorker (
            window,
            prefix
                + "2.pause",
            LfpWindowsUiaAction::
                queryToggle);
    EXPECT_EQ (
        pauseResult.invokeResult,
        S_OK);

    const auto invokeResult =
        invokeWindowsUiaFromWorker (
            window,
            prefix
                + "3.pane_selector",
            LfpWindowsUiaAction::
                invoke);
    EXPECT_EQ (
        invokeResult.invokeResult,
        S_OK);
    EXPECT_TRUE (
        canvas->isPaneActive (2));
    const auto reselectResult =
        invokeWindowsUiaFromWorker (
            window,
            secondId,
            LfpWindowsUiaAction::
                select);
    EXPECT_EQ (
        reselectResult.invokeResult,
        S_OK);
    EXPECT_TRUE (
        canvas->isPaneActive (1));

    splitters[1]
        ->timescale
        ->setEnabled (
            false);
    const auto disabledResult =
        invokeWindowsUiaFromWorker (
            window,
            secondId,
            LfpWindowsUiaAction::
                select);
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    splitters[1]
        ->timescale
        ->setEnabled (
            true);

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::SINGLE);
    const auto hiddenResult =
        invokeWindowsUiaFromWorker (
            window,
            secondId,
            LfpWindowsUiaAction::
                querySelection);
    EXPECT_EQ (
        hiddenResult.invokeResult,
        E_FAIL);

    canvas->removeFromDesktop();
    const auto closedResult =
        invokeWindowsUiaFromWorker (
            window,
            firstId,
            LfpWindowsUiaAction::
                querySelection);
    EXPECT_EQ (
        closedResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
}
#endif

TEST_F (LfpDisplayNodeTests,
        DisplayBuffersRetainTheirStableStreamKeys)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    3 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 3);
    const auto streams =
        multiStreamProcessor
            ->getDataStreams();
    ASSERT_EQ (streams.size(), 3);
    EXPECT_EQ (
        buffers[0]->streamKey,
        streams[0]->getKey());
    EXPECT_EQ (
        buffers[1]->streamKey,
        streams[1]->getKey());
    EXPECT_EQ (
        buffers[2]->streamKey,
        streams[2]->getKey());
}

TEST_F (LfpDisplayNodeTests,
        StreamXmlRestoreImmediatelySynchronisesEveryPaneState)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    3 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto* multiStreamSource =
        dynamic_cast<
            FakeSourceNode*> (
            multiStreamTester
                ->getSourceNode());
    ASSERT_NE (
        multiStreamSource,
        nullptr);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            multiStreamProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();

    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3);
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 3);
    buffers[1]->sampleRate =
        23456.0f;
    buffers[1]->numChannels = 2;
    while (buffers[1]
               ->channelMetadata
               .size()
           > 2)
    {
        buffers[1]
            ->channelMetadata
            .removeLast();
    }
    buffers[1]
        ->channelMetadata
        .getReference (0)
        .name =
        "XML_SECOND";
    const std::array<int, 3>
        selectedBufferIndices {
            1,
            2,
            1
        };
    XmlElement savedRoot ("ROOT");
    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        const auto bufferIndex =
            selectedBufferIndices[
                paneIndex];
        splitters[paneIndex]
            ->streamSelection
            ->setSelectedId (
                buffers[bufferIndex]
                    ->id,
                sendNotificationSync);
        splitters[paneIndex]
            ->options
            ->saveParameters (
                &savedRoot);
    }

    const auto verifyPaneState =
        [&] (int paneIndex,
             int expectedBufferIndex)
        {
            auto* splitter =
                splitters[paneIndex];
            auto* expectedBuffer =
                buffers[
                    expectedBufferIndex];
            EXPECT_EQ (
                splitter
                    ->selectedStreamKey,
                expectedBuffer
                    ->streamKey);
            EXPECT_EQ (
                splitter
                    ->selectedStreamId,
                expectedBuffer->id);
            EXPECT_EQ (
                splitter
                    ->displayBuffer,
                expectedBuffer);
            EXPECT_EQ (
                splitter
                    ->streamSelection
                    ->getSelectedId(),
                expectedBuffer->id);
            EXPECT_EQ (
                splitter
                    ->streamSelection
                    ->getText(),
                expectedBuffer->name);
            EXPECT_EQ (
                splitter->nChans,
                expectedBuffer
                    ->numChannels);
            EXPECT_FLOAT_EQ (
                splitter
                    ->getDrawableSampleRate(),
                expectedBuffer
                    ->sampleRate);
            EXPECT_EQ (
                splitter
                    ->lfpDisplay
                    ->getNumChannels(),
                expectedBuffer
                    ->numChannels);
            ASSERT_FALSE (
                splitter
                    ->lfpDisplay
                    ->channels
                    .isEmpty());
            EXPECT_EQ (
                splitter
                    ->lfpDisplay
                    ->channels[0]
                    ->getName(),
                expectedBuffer
                    ->channelMetadata[0]
                    .name);
            for (int bufferIndex = 0;
                 bufferIndex < 3;
                 ++bufferIndex)
            {
                EXPECT_EQ (
                    buffers[bufferIndex]
                        ->displays
                        .contains (
                            paneIndex),
                    bufferIndex
                        == expectedBufferIndex);
            }
        };

    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        splitters[paneIndex]
            ->streamSelection
            ->setSelectedId (
                buffers[0]->id,
                sendNotificationSync);
        splitters[paneIndex]
            ->options
            ->loadParameters (
                &savedRoot);
        verifyPaneState (
            paneIndex,
            selectedBufferIndices[
                paneIndex]);
    }

    XmlElement staleRoot (
        savedRoot);
    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        auto* pane =
            staleRoot
                .getChildByName (
                    "LFPDISPLAY"
                    + String (
                        paneIndex));
        ASSERT_NE (pane, nullptr);
        pane->setAttribute (
            "stream_key",
            "missing|stream");
        splitters[paneIndex]
            ->options
            ->loadParameters (
                &staleRoot);
        verifyPaneState (
            paneIndex,
            0);
    }

    XmlElement missingRoot (
        savedRoot);
    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        auto* pane =
            missingRoot
                .getChildByName (
                    "LFPDISPLAY"
                    + String (
                        paneIndex));
        ASSERT_NE (pane, nullptr);
        pane->removeAttribute (
            "stream_key");
        splitters[paneIndex]
            ->options
            ->loadParameters (
                &missingRoot);
        verifyPaneState (
            paneIndex,
            0);
    }

    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        canvas
            ->removeBufferForDisplay (
                paneIndex,
                splitters[paneIndex]
                    ->displayBuffer);
    }
    multiStreamSource
        ->setStreamCountPreservingExisting (
            3,
            0);
    multiStreamTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    XmlElement zeroRoot (
        "ROOT");
    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        splitters[paneIndex]
            ->options
            ->saveParameters (
                &zeroRoot);
        splitters[paneIndex]
            ->options
            ->loadParameters (
                &zeroRoot);
        EXPECT_TRUE (
            splitters[paneIndex]
                ->selectedStreamKey
                .isEmpty());
        EXPECT_EQ (
            splitters[paneIndex]
                ->selectedStreamId,
            0);
        EXPECT_EQ (
            splitters[paneIndex]
                ->displayBuffer,
            nullptr);
        EXPECT_EQ (
            splitters[paneIndex]
                ->streamSelection
                ->getSelectedId(),
            0);
    }
}

TEST_F (LfpDisplayNodeTests,
        StreamSelectionSurvivesSourceRebuildAndHandlesZeroInputs)
{
    auto dynamicTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    3 }),
            TestGuiRuntimeLifetime::
                process);
    auto* dynamicProcessor =
        dynamicTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    dynamicProcessor
        ->setHeadlessMode (
            false);
    dynamicProcessor
        ->setProcessorType (
            Plugin::Processor::
                SPLITTER);
    auto* editor =
        static_cast<
            LfpViewer::
                LfpDisplayEditor*> (
            dynamicProcessor
                ->createEditor());
    dynamicProcessor
        ->setProcessorType (
            Plugin::Processor::SINK);
    ASSERT_NE (editor, nullptr);
    editor->setBounds (
        0,
        0,
        900,
        800);
    editor->addToDesktop (0);
    editor->setVisible (true);
    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            10);
    dynamicProcessor
        ->setHeadlessMode (
            true);
    editor->canvas.reset (
        editor->createNewCanvas());
    auto* canvas =
        dynamic_cast<
            LfpViewer::
                LfpDisplayCanvas*> (
            editor->canvas.get());
    ASSERT_NE (canvas, nullptr);
    canvas->updateSettings();
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* splitter = splitters[0];
    auto* source =
        dynamic_cast<
            FakeSourceNode*> (
            dynamicTester
                ->getSourceNode());
    ASSERT_NE (source, nullptr);
    auto retainedStreamHandler =
        splitter
            ->streamSelection
            ->createAccessibilityHandler();
    ASSERT_NE (
        retainedStreamHandler,
        nullptr);

    auto buffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 3);
    splitter
        ->streamSelection
        ->setSelectedId (
            buffers[1]->id,
            sendNotificationSync);
    const auto retainedKey =
        buffers[1]->streamKey;
    ASSERT_EQ (
        splitter->selectedStreamKey,
        retainedKey);

    source
        ->setStreamCountPreservingExisting (
            4,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    buffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 4);
    EXPECT_EQ (
        splitter->selectedStreamKey,
        retainedKey);
    ASSERT_NE (
        splitter->displayBuffer,
        nullptr);
    ASSERT_TRUE (
        buffers.contains (
            splitter->displayBuffer));
    EXPECT_EQ (
        splitter->displayBuffer
            ->streamKey,
        retainedKey);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getSelectedId(),
        splitter->selectedStreamId);

    source->moveStream (
        1,
        0);
    dynamicTester
        ->updateSourceNodeSettings();
    buffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 4);
    ASSERT_NE (
        splitter->displayBuffer,
        nullptr);
    EXPECT_EQ (
        splitter->selectedStreamKey,
        retainedKey);
    EXPECT_EQ (
        splitter->displayBuffer
            ->streamKey,
        retainedKey);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getSelectedId(),
        splitter->selectedStreamId);
    source->moveStream (
        0,
        1);
    dynamicTester
        ->updateSourceNodeSettings();

    source
        ->setStreamCountPreservingExisting (
            2,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    const auto shrunkBuffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (
        shrunkBuffers.size(),
        2);
    ASSERT_NE (
        splitter->displayBuffer,
        nullptr);
    ASSERT_TRUE (
        shrunkBuffers.contains (
            splitter->displayBuffer));
    EXPECT_EQ (
        splitter->selectedStreamKey,
        retainedKey);
    EXPECT_EQ (
        splitter->displayBuffer
            ->streamKey,
        retainedKey);

    source
        ->setStreamCountPreservingExisting (
            1,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    buffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 1);
    EXPECT_EQ (
        splitter->selectedStreamKey,
        buffers[0]->streamKey);
    EXPECT_EQ (
        splitter->selectedStreamId,
        buffers[0]->id);
    EXPECT_EQ (
        splitter->displayBuffer,
        buffers[0]);

    source
        ->setStreamCountPreservingExisting (
            1,
            0);
    dynamicTester
        ->updateSourceNodeSettings();
    EXPECT_TRUE (
        dynamicProcessor
            ->getDisplayBuffers()
            .isEmpty());
    EXPECT_TRUE (
        splitter
            ->selectedStreamKey
            .isEmpty());
    EXPECT_EQ (
        splitter->selectedStreamId,
        0);
    EXPECT_EQ (
        splitter->displayBuffer,
        nullptr);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getSelectedId(),
        0);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getText(),
        "No data streams");
    EXPECT_FALSE (
        splitter
            ->streamSelection
            ->isEnabled());
    EXPECT_FALSE (
        retainedStreamHandler
            ->isEnabled());
    auto* emptyValue =
        retainedStreamHandler
            ->getValueInterface();
    ASSERT_NE (emptyValue, nullptr);
    EXPECT_FALSE (
        emptyValue->isReadOnly());
    EXPECT_EQ (
        emptyValue
            ->getCurrentValueAsString(),
        "No data streams");
    String workerValue;
    bool workerEnabled = true;
    std::thread zeroStateWorker (
        [&]
        {
            workerValue =
                emptyValue
                    ->getCurrentValueAsString();
            workerEnabled =
                retainedStreamHandler
                    ->isEnabled();
        });
    zeroStateWorker.join();
    EXPECT_EQ (
        workerValue,
        "No data streams");
    EXPECT_FALSE (
        workerEnabled);

    XmlElement emptyRoot (
        "ROOT");
    splitter->options
        ->saveParameters (
            &emptyRoot);
    auto* emptyPane =
        emptyRoot
            .getChildByName (
                "LFPDISPLAY0");
    ASSERT_NE (
        emptyPane,
        nullptr);
    EXPECT_TRUE (
        emptyPane
            ->getStringAttribute (
                "stream_key")
            .isEmpty());
    splitter->options
        ->loadParameters (
            &emptyRoot);
    EXPECT_TRUE (
        splitter
            ->selectedStreamKey
            .isEmpty());
    EXPECT_EQ (
        splitter->displayBuffer,
        nullptr);

    source
        ->setStreamCountPreservingExisting (
            2,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    buffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    EXPECT_TRUE (
        splitter
            ->streamSelection
            ->isEnabled());
    EXPECT_EQ (
        splitter->selectedStreamKey,
        buffers[0]->streamKey);
    EXPECT_EQ (
        splitter->selectedStreamId,
        buffers[0]->id);
    EXPECT_EQ (
        splitter->displayBuffer,
        buffers[0]);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getText(),
        buffers[0]->name);
    String readdedWorkerValue;
    std::thread readdedStateWorker (
        [&]
        {
            readdedWorkerValue =
                emptyValue
                    ->getCurrentValueAsString();
        });
    readdedStateWorker.join();
    EXPECT_EQ (
        readdedWorkerValue,
        buffers[0]->name);

    editor->canvas.reset();
    String detachedWorkerValue;
    std::thread detachedStateWorker (
        [&]
        {
            detachedWorkerValue =
                emptyValue
                    ->getCurrentValueAsString();
        });
    detachedStateWorker.join();
    EXPECT_EQ (
        detachedWorkerValue,
        buffers[0]->name);
    EXPECT_TRUE (
        emptyValue->isReadOnly());
    dynamicProcessor
        ->setHeadlessMode (
            true);
}

TEST_F (LfpDisplayNodeTests,
        DuplicateStableStreamKeysFailClosed)
{
    auto duplicateTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* duplicateProcessor =
        duplicateTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto* duplicateSource =
        dynamic_cast<
            FakeSourceNode*> (
            duplicateTester
                ->getSourceNode());
    ASSERT_NE (
        duplicateSource,
        nullptr);
    duplicateSource->setStreamName (
        1,
        "FakeSourceNode0");
    duplicateTester
        ->updateSourceNodeSettings();
    const auto buffers =
        duplicateProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    ASSERT_EQ (
        buffers[0]->streamKey,
        buffers[1]->streamKey);

    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            duplicateProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3);
    for (int splitterIndex = 0;
         splitterIndex
         < splitters.size();
         ++splitterIndex)
    {
        auto* splitter =
            splitters[
                splitterIndex];
        EXPECT_TRUE (
            splitter
                ->selectedStreamKey
                .isEmpty());
        EXPECT_EQ (
            splitter
                ->selectedStreamId,
            0);
        EXPECT_EQ (
            splitter
                ->displayBuffer,
            nullptr);
        EXPECT_EQ (
            splitter
                ->streamSelection
                ->getSelectedId(),
            0);
        EXPECT_EQ (
            splitter
                ->streamSelection
                ->getText(),
            "Ambiguous data streams");
        EXPECT_FALSE (
            splitter
                ->streamSelection
                ->isEnabled());

        XmlElement root (
            "ROOT");
        splitter->options
            ->saveParameters (
                &root);
        auto* pane =
            root.getChildByName (
                "LFPDISPLAY"
                + String (
                    splitterIndex));
        ASSERT_NE (pane, nullptr);
        EXPECT_TRUE (
            pane
                ->getStringAttribute (
                    "stream_key")
                .isEmpty());
    }
}

TEST_F (LfpDisplayNodeTests,
        InvalidStreamRuntimeSelectionLeavesCanonicalStateUnchanged)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* splitter = splitters[0];
    const auto buffers =
        processor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 1);
    const auto expectedKey =
        splitter->selectedStreamKey;
    const auto expectedId =
        splitter->selectedStreamId;
    auto* const expectedBuffer =
        splitter->displayBuffer;
    const auto expectedText =
        splitter
            ->streamSelection
            ->getText();

    splitter
        ->streamSelection
        ->setSelectedId (
            999999,
            sendNotificationSync);

    EXPECT_EQ (
        splitter->selectedStreamKey,
        expectedKey);
    EXPECT_EQ (
        splitter->selectedStreamId,
        expectedId);
    EXPECT_EQ (
        splitter->displayBuffer,
        expectedBuffer);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getSelectedId(),
        expectedId);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getText(),
        expectedText);
}

TEST_F (LfpDisplayNodeTests,
        StableKeySelectionFromWorkerRunsOnMessageThreadAndSynchronisesState)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            multiStreamProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* splitter = splitters[0];
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    ASSERT_TRUE (
        buffers[0]->displays
            .contains (0));
    ASSERT_FALSE (
        buffers[1]->displays
            .contains (0));
    auto streamHandler =
        splitter
            ->streamSelection
            ->createAccessibilityHandler();
    ASSERT_NE (
        streamHandler,
        nullptr);
    auto* streamValue =
        streamHandler
            ->getValueInterface();
    ASSERT_NE (
        streamValue,
        nullptr);
    LfpThreadTrackingComboBoxListener
        listener;
    splitter
        ->streamSelection
        ->addListener (
            &listener);
    std::atomic<bool>
        workerReturned { false };
    bool selectionResult = false;
    std::thread worker (
        [&]
        {
            selectionResult =
                splitter
                    ->selectStreamByKey (
                        buffers[1]
                            ->streamKey);
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
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
    splitter
        ->streamSelection
        ->removeListener (
            &listener);

    EXPECT_TRUE (
        selectionResult);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        buffers[1]->streamKey);
    EXPECT_EQ (
        splitter
            ->selectedStreamId,
        buffers[1]->id);
    EXPECT_EQ (
        splitter
            ->displayBuffer,
        buffers[1]);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getSelectedId(),
        buffers[1]->id);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getText(),
        buffers[1]->name);
    EXPECT_EQ (
        streamValue
            ->getCurrentValueAsString(),
        buffers[1]->name);
    EXPECT_FALSE (
        buffers[0]->displays
            .contains (0));
    EXPECT_TRUE (
        buffers[1]->displays
            .contains (0));
}

TEST_F (LfpDisplayNodeTests,
        WorkerStreamSelectionRefreshesDerivedDisplayState)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            multiStreamProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    auto* splitter =
        getDisplaySplitters (
            *canvas)[0];
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[1]->sampleRate =
        12345.0f;
    buffers[1]->numChannels = 2;
    while (buffers[1]
               ->channelMetadata
               .size()
           > 2)
    {
        buffers[1]
            ->channelMetadata
            .removeLast();
    }
    buffers[1]
        ->channelMetadata
        .getReference (0)
        .name =
        "SECOND_A";
    buffers[1]
        ->channelMetadata
        .getReference (0)
        .group = 7;
    buffers[1]
        ->channelMetadata
        .getReference (0)
        .ypos = 321.0f;
    buffers[1]
        ->channelMetadata
        .getReference (0)
        .hasGroupMetadata = true;
    buffers[1]
        ->channelMetadata
        .getReference (0)
        .hasYposMetadata = true;
    buffers[1]
        ->channelMetadata
        .getReference (1)
        .name =
        "SECOND_B";
    ASSERT_EQ (
        splitter->nChans,
        4);
    ASSERT_EQ (
        splitter
            ->lfpDisplay
            ->getNumChannels(),
        4);

    std::atomic<bool>
        workerReturned { false };
    bool selectionResult = false;
    std::thread worker (
        [&]
        {
            selectionResult =
                splitter
                    ->selectStreamByKey (
                        buffers[1]
                            ->streamKey);
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
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);

    ASSERT_TRUE (
        selectionResult);
    EXPECT_EQ (
        splitter->nChans,
        2);
    EXPECT_EQ (
        splitter
            ->lfpDisplay
            ->getNumChannels(),
        2);
    EXPECT_FLOAT_EQ (
        splitter
            ->getDrawableSampleRate(),
        12345.0f);
    ASSERT_EQ (
        splitter
            ->lfpDisplay
            ->channels
            .size(),
        2);
    EXPECT_EQ (
        splitter
            ->lfpDisplay
            ->channels[0]
            ->getName(),
        "SECOND_A");
    EXPECT_EQ (
        splitter
            ->lfpDisplay
            ->channels[0]
            ->getGroup(),
        7);
    EXPECT_FLOAT_EQ (
        splitter
            ->lfpDisplay
            ->channels[0]
            ->getDepth(),
        321.0f);
    EXPECT_TRUE (
        splitter
            ->lfpDisplay
            ->channels[0]
            ->hasGroupMetadata());
    EXPECT_TRUE (
        splitter
            ->lfpDisplay
            ->channels[0]
            ->hasYposMetadata());
}

TEST_F (LfpDisplayNodeTests,
        StreamAccessibilityValueUsesCanonicalGuiSelectionAndLifecycle)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            multiStreamProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (
        600,
        800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    auto* splitter =
        getDisplaySplitters (
            *canvas)[0];
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    buffers[1]->sampleRate =
        12345.0f;
    buffers[1]->numChannels = 2;
    while (buffers[1]
               ->channelMetadata
               .size()
           > 2)
    {
        buffers[1]
            ->channelMetadata
            .removeLast();
    }
    buffers[1]
        ->channelMetadata
        .getReference (0)
        .name =
        "VALUE_SECOND";
    auto retainedHandler =
        splitter
            ->streamSelection
            ->createAccessibilityHandler();
    ASSERT_NE (
        retainedHandler,
        nullptr);
    auto* retainedValue =
        retainedHandler
            ->getValueInterface();
    ASSERT_NE (
        retainedValue,
        nullptr);
    auto* writer =
        dynamic_cast<
            AccessibilityValueStringWriter*> (
            retainedValue);
    ASSERT_NE (writer, nullptr);
    EXPECT_FALSE (
        retainedValue
            ->isReadOnly());
    LfpThreadTrackingComboBoxListener
        listener;
    splitter
        ->streamSelection
        ->addListener (
            &listener);
    const auto writeFromWorker =
        [&] (const String& value)
    {
        bool accepted = false;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                accepted =
                    writer
                        ->setValueAsStringIfSupported (
                            value);
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
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return accepted;
    };

    EXPECT_TRUE (
        writeFromWorker (
            "FakeSourceNode1"));
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        buffers[1]->streamKey);
    EXPECT_EQ (
        splitter->nChans,
        2);
    EXPECT_FLOAT_EQ (
        splitter
            ->getDrawableSampleRate(),
        12345.0f);
    EXPECT_EQ (
        splitter
            ->lfpDisplay
            ->getNumChannels(),
        2);
    EXPECT_EQ (
        splitter
            ->lfpDisplay
            ->channels[0]
            ->getName(),
        "VALUE_SECOND");
    EXPECT_FALSE (
        writeFromWorker (
            "not a stream"));
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        buffers[1]->streamKey);

    splitter
        ->streamSelection
        ->setEnabled (
            false);
    EXPECT_FALSE (
        writeFromWorker (
            "FakeSourceNode0"));
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    splitter
        ->streamSelection
        ->setEnabled (
            true);
    EXPECT_TRUE (
        writeFromWorker (
            "FakeSourceNode0"));
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        buffers[0]->streamKey);

    splitter
        ->streamSelection
        ->removeListener (
            &listener);
    canvas.reset();
    EXPECT_FALSE (
        writeFromWorker (
            "FakeSourceNode1"));
    EXPECT_EQ (
        retainedValue
            ->getCurrentValueAsString(),
        "FakeSourceNode0");
}

TEST_F (LfpDisplayNodeTests,
        RemovedSelectedBufferCallbackImmediatelyClearsCanonicalState)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            multiStreamProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* removedSplitter =
        splitters[0];
    auto* unaffectedSplitter =
        splitters[1];
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    removedSplitter
        ->streamSelection
        ->setSelectedId (
            buffers[1]->id,
            sendNotificationSync);
    ASSERT_EQ (
        removedSplitter
            ->displayBuffer,
        buffers[1]);
    ASSERT_EQ (
        unaffectedSplitter
            ->displayBuffer,
        buffers[0]);
    ASSERT_TRUE (
        buffers[1]->displays
            .contains (0));
    ASSERT_TRUE (
        buffers[0]->displays
            .contains (1));
    const auto unaffectedKey =
        unaffectedSplitter
            ->selectedStreamKey;
    const auto unaffectedId =
        unaffectedSplitter
            ->selectedStreamId;
    auto unaffectedBuffer =
        unaffectedSplitter
            ->displayBuffer;
    auto retainedHandler =
        removedSplitter
            ->streamSelection
            ->createAccessibilityHandler();
    ASSERT_NE (
        retainedHandler,
        nullptr);
    auto* retainedValue =
        retainedHandler
            ->getValueInterface();
    ASSERT_NE (
        retainedValue,
        nullptr);

    canvas
        ->removeBufferForDisplay (
            1,
            buffers[1]);
    ASSERT_EQ (
        unaffectedSplitter
            ->displayBuffer,
        unaffectedBuffer);
    ASSERT_EQ (
        unaffectedSplitter
            ->selectedStreamKey,
        unaffectedKey);
    ASSERT_TRUE (
        buffers[0]->displays
            .contains (1));

    canvas
        ->removeBufferForDisplay (
            0,
            buffers[1]);

    EXPECT_EQ (
        removedSplitter
            ->displayBuffer,
        nullptr);
    EXPECT_TRUE (
        removedSplitter
            ->selectedStreamKey
            .isEmpty());
    EXPECT_EQ (
        removedSplitter
            ->selectedStreamId,
        0);
    EXPECT_EQ (
        removedSplitter
            ->streamSelection
            ->getSelectedId(),
        0);
    EXPECT_EQ (
        removedSplitter
            ->streamSelection
            ->getText(),
        "Updating data streams");
    EXPECT_FALSE (
        removedSplitter
            ->streamSelection
            ->isEnabled());
    EXPECT_FALSE (
        retainedHandler
            ->isEnabled());
    EXPECT_FALSE (
        buffers[1]->displays
            .contains (0));
    EXPECT_EQ (
        unaffectedSplitter
            ->selectedStreamKey,
        unaffectedKey);
    EXPECT_EQ (
        unaffectedSplitter
            ->selectedStreamId,
        unaffectedId);
    EXPECT_EQ (
        unaffectedSplitter
            ->displayBuffer,
        unaffectedBuffer);
    EXPECT_TRUE (
        buffers[0]->displays
            .contains (1));
    String workerValue;
    std::thread worker (
        [&]
        {
            workerValue =
                retainedValue
                    ->getCurrentValueAsString();
        });
    worker.join();
    EXPECT_EQ (
        workerValue,
        "Updating data streams");
}

TEST_F (LfpDisplayNodeTests,
        SourceUpdateClearsRemovedStreamFromEveryPaneBeforeFallback)
{
    auto dynamicTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* dynamicProcessor =
        dynamicTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    dynamicProcessor
        ->setHeadlessMode (
            false);
    dynamicProcessor
        ->setProcessorType (
            Plugin::Processor::
                SPLITTER);
    auto* editor =
        static_cast<
            LfpViewer::
                LfpDisplayEditor*> (
            dynamicProcessor
                ->createEditor());
    dynamicProcessor
        ->setProcessorType (
            Plugin::Processor::SINK);
    ASSERT_NE (editor, nullptr);
    dynamicProcessor
        ->setHeadlessMode (
            true);
    editor->canvas.reset (
        editor->createNewCanvas());
    auto* canvas =
        dynamic_cast<
            LfpViewer::
                LfpDisplayCanvas*> (
            editor->canvas.get());
    ASSERT_NE (canvas, nullptr);
    canvas->updateSettings();
    canvas->setSize (
        600,
        800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* source =
        dynamic_cast<
            FakeSourceNode*> (
            dynamicTester
                ->getSourceNode());
    ASSERT_NE (source, nullptr);
    const auto buffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    auto* removedBuffer =
        buffers[1];
    std::array<
        std::unique_ptr<
            AccessibilityHandler>,
        3>
        handlers;
    std::array<
        AccessibilityValueInterface*,
        3>
        values {};
    LfpStreamRemovalSnapshotState
        snapshotState;
    std::array<
        std::unique_ptr<
            LfpStreamRemovalSnapshotListener>,
        3>
        snapshotListeners;
    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        splitters[paneIndex]
            ->streamSelection
            ->setSelectedId (
                removedBuffer->id,
                sendNotificationSync);
        ASSERT_EQ (
            splitters[paneIndex]
                ->displayBuffer,
            removedBuffer);
        handlers[paneIndex] =
            splitters[paneIndex]
                ->streamSelection
                ->createAccessibilityHandler();
        ASSERT_NE (
            handlers[paneIndex],
            nullptr);
        values[paneIndex] =
            handlers[paneIndex]
                ->getValueInterface();
        ASSERT_NE (
            values[paneIndex],
            nullptr);
        snapshotListeners[
            paneIndex] =
            std::make_unique<
                LfpStreamRemovalSnapshotListener> (
                *splitters[
                    paneIndex],
                *removedBuffer,
                *values[
                    paneIndex],
                snapshotState);
        splitters[paneIndex]
            ->streamSelection
            ->addComponentListener (
                snapshotListeners[
                    paneIndex]
                    .get());
    }
    ASSERT_EQ (
        removedBuffer
            ->displays
            .size(),
        3);
    LfpThreadTrackingComboBoxListener
        selectionListener;
    splitters[0]
        ->streamSelection
        ->addListener (
            &selectionListener);
    splitters[0]
        ->streamSelection
        ->showPopup();
    ASSERT_TRUE (
        splitters[0]
            ->streamSelection
            ->isPopupActive());

    source
        ->setStreamCountPreservingExisting (
            1,
            4);
    dynamicTester
        ->updateSourceNodeSettings();

    EXPECT_EQ (
        snapshotState.callbackCount,
        3);
    EXPECT_EQ (
        snapshotState
            .lastMembershipCount,
        0);
    for (int attempt = 0;
         attempt < 100
             && ! splitters[0]
                       ->streamSelection
                       ->isEnabled();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    const auto remainingBuffers =
        dynamicProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (
        remainingBuffers.size(),
        1);
    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        splitters[paneIndex]
            ->streamSelection
            ->removeComponentListener (
                snapshotListeners[
                    paneIndex]
                    .get());
        EXPECT_TRUE (
            snapshotListeners[
                paneIndex]
                ->pointerWasNull);
        EXPECT_TRUE (
            snapshotListeners[
                paneIndex]
                ->keyWasEmpty);
        EXPECT_TRUE (
            snapshotListeners[
                paneIndex]
                ->idWasZero);
        EXPECT_TRUE (
            snapshotListeners[
                paneIndex]
                ->comboIdWasZero);
        EXPECT_EQ (
            snapshotListeners[
                paneIndex]
                ->comboText,
            "Updating data streams");
        EXPECT_EQ (
            snapshotListeners[
                paneIndex]
                ->accessibleValue,
            "Updating data streams");
        EXPECT_EQ (
            splitters[paneIndex]
                ->selectedStreamKey,
            remainingBuffers[0]
                ->streamKey);
        EXPECT_EQ (
            splitters[paneIndex]
                ->selectedStreamId,
            remainingBuffers[0]->id);
        EXPECT_EQ (
            splitters[paneIndex]
                ->displayBuffer,
            remainingBuffers[0]);
        EXPECT_EQ (
            splitters[paneIndex]
                ->streamSelection
                ->getSelectedId(),
            remainingBuffers[0]->id);
        EXPECT_TRUE (
            splitters[paneIndex]
                ->streamSelection
                ->isEnabled());
        EXPECT_EQ (
            values[paneIndex]
                ->getCurrentValueAsString(),
            remainingBuffers[0]->name);
        EXPECT_TRUE (
            remainingBuffers[0]
                ->displays
                .contains (
                    paneIndex));
    }
    splitters[0]
        ->streamSelection
        ->removeListener (
            &selectionListener);
    EXPECT_EQ (
        selectionListener
            .callbackCount
            .load(),
        0);

    editor->canvas.reset();
    dynamicProcessor
        ->setHeadlessMode (
            true);
}

TEST_F (LfpDisplayNodeTests,
        WorkerStreamSelectionQueuedBeforeDisableIsRejected)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            multiStreamProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    auto* splitter =
        getDisplaySplitters (
            *canvas)[0];
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    std::atomic<bool>
        workerStarted { false };
    std::atomic<bool>
        workerReturned { false };
    bool selectionResult = true;
    std::thread worker (
        [&]
        {
            workerStarted.store (
                true);
            selectionResult =
                splitter
                    ->selectStreamByKey (
                        buffers[1]
                            ->streamKey);
            workerReturned.store (
                true);
        });
    while (! workerStarted.load())
        std::this_thread::yield();
    splitter
        ->streamSelection
        ->setEnabled (
            false);
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);

    EXPECT_FALSE (
        selectionResult);
    EXPECT_FALSE (
        splitter
            ->streamSelection
            ->isEnabled());
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        buffers[0]->streamKey);
    EXPECT_EQ (
        splitter
            ->selectedStreamId,
        buffers[0]->id);
    EXPECT_EQ (
        splitter
            ->displayBuffer,
        buffers[0]);
    EXPECT_TRUE (
        buffers[0]->displays
            .contains (0));
    EXPECT_FALSE (
        buffers[1]->displays
            .contains (0));
}

TEST_F (LfpDisplayNodeTests,
        WorkerStreamSelectionReturnsSafelyWhenNotificationDestroysCanvas)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            multiStreamProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    auto* splitter =
        getDisplaySplitters (
            *canvas)[0];
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    const auto secondKey =
        buffers[1]->streamKey;
    LfpDestroyCanvasComboBoxListener
        destroyListener (
            canvas);
    splitter
        ->streamSelection
        ->addListener (
            &destroyListener);
    std::atomic<bool>
        workerReturned { false };
    bool selectionResult = true;
    std::thread worker (
        [&]
        {
            selectionResult =
                splitter
                    ->selectStreamByKey (
                        secondKey);
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
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);

    EXPECT_EQ (
        canvas,
        nullptr);
    EXPECT_FALSE (
        selectionResult);
}

TEST_F (LfpDisplayNodeTests,
        ExposesStableStreamSelectorForEveryDisplay)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto expectedValue =
        processor
            ->getDisplayBuffers()[0]
            ->name;
    std::array<Component*, 3>
        selectors {};

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".stream";
        selectors[displayIndex] =
            findLfpDescendantById (
                *canvas,
                id);
        ASSERT_NE (
            selectors[displayIndex],
            nullptr)
            << id;
        auto* handler =
            selectors[displayIndex]
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " stream");
        const auto description =
            "Choose the data stream shown in LFP display "
            + String (
                displayNumber)
            + ".";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            "Choose the data stream shown in LFP display "
                + String (
                    displayNumber)
                + ". This changes display routing only; acquisition and recording are unaffected.");
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_FALSE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            expectedValue);
        auto* comboBox =
            dynamic_cast<
                MessageThreadComboBox*> (
                selectors[
                    displayIndex]);
        ASSERT_NE (comboBox, nullptr);
        ASSERT_EQ (
            comboBox->getNumItems(),
            1);
        PopupMenu::MenuItemIterator
            choice (
                *comboBox
                     ->getRootMenu(),
                false);
        ASSERT_TRUE (
            choice.next());
        EXPECT_EQ (
            choice
                .getItem()
                .text,
            "FakeSourceNode0");
        EXPECT_EQ (
            choice
                .getItem()
                .accessibilityId,
            id
                + ".choice.2_fakesourcenode0_327c46616b65536f757263654e6f646530");
        EXPECT_TRUE (
            isValidSemanticId (
                choice
                    .getItem()
                    .accessibilityId));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        showMenu));
        EXPECT_EQ (
            selectors[displayIndex]
                ->getParentComponent()
                ->isVisible(),
            displayIndex == 0);
    }

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_TRUE (
            selectors[displayIndex]
                ->getParentComponent()
                ->isVisible());
        EXPECT_EQ (
            selectors[displayIndex]
                ->getComponentID(),
            prefix
                + String (
                    displayIndex + 1)
                + ".stream");
    }
}

TEST_F (LfpDisplayNodeTests,
        StreamChoicesDisambiguateEqualNamesByRealSourceAndStableKey)
{
    auto multiStreamTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* multiStreamProcessor =
        multiStreamTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto* source =
        dynamic_cast<
            FakeSourceNode*> (
            multiStreamTester
                ->getSourceNode());
    ASSERT_NE (source, nullptr);
    source->setStreamName (
        0,
        "Shared");
    source->setStreamName (
        1,
        "Shared");
    source->setStreamSourceNodeId (
        0,
        17);
    source->setStreamSourceNodeId (
        1,
        29);
    multiStreamTester
        ->updateSourceNodeSettings();
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            multiStreamProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    auto* splitter =
        getDisplaySplitters (
            *canvas)[0];
    const auto buffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (buffers.size(), 2);
    EXPECT_EQ (
        buffers[0]->streamKey,
        "17|Shared");
    EXPECT_EQ (
        buffers[1]->streamKey,
        "29|Shared");
    ASSERT_EQ (
        splitter
            ->streamSelection
            ->getNumItems(),
        2);
    const std::array<String, 2>
        expectedLabels {
            "Shared (source 17)",
            "Shared (source 29)"
        };
    const std::array<String, 2>
        expectedIds {
            splitter
                    ->streamSelection
                    ->getComponentID()
                + ".choice.17_shared_31377c536861726564",
            splitter
                    ->streamSelection
                    ->getComponentID()
                + ".choice.29_shared_32397c536861726564"
        };
    int choiceIndex = 0;
    for (PopupMenu::MenuItemIterator
             iterator (
                 *splitter
                      ->streamSelection
                      ->getRootMenu(),
                 false);
         iterator.next();)
    {
        ASSERT_LT (
            choiceIndex,
            2);
        EXPECT_EQ (
            iterator
                .getItem()
                .text,
            expectedLabels[
                choiceIndex]);
        EXPECT_EQ (
            iterator
                .getItem()
                .accessibilityId,
            expectedIds[
                choiceIndex]);
        EXPECT_TRUE (
            isValidSemanticId (
                iterator
                    .getItem()
                    .accessibilityId));
        ++choiceIndex;
    }
    EXPECT_EQ (
        choiceIndex,
        2);
    EXPECT_NE (
        expectedIds[0],
        expectedIds[1]);
    ASSERT_EQ (
        splitter->nChans,
        4);
    ASSERT_FLOAT_EQ (
        splitter
            ->getDrawableSampleRate(),
        sampleRate);
    ASSERT_EQ (
        splitter
            ->lfpDisplay
            ->getNumChannels(),
        4);
    splitter
        ->screenBufferIndex
        .set (
            0,
            73);
    splitter
        ->lastScreenBufferIndex
        .set (
            0,
            91);

    source->setStreamSourceNodeId (
        1,
        17);
    multiStreamTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    EXPECT_FALSE (
        splitter
            ->streamSelection
            ->isEnabled());
    EXPECT_TRUE (
        splitter
            ->selectedStreamKey
            .isEmpty());
    EXPECT_EQ (
        splitter
            ->displayBuffer,
        nullptr);
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getText(),
        "Ambiguous data streams");
    EXPECT_EQ (
        splitter
            ->streamSelection
            ->getNumItems(),
        0);
    PopupMenu::MenuItemIterator
        ambiguousChoice (
            *splitter
                 ->streamSelection
                 ->getRootMenu(),
            false);
    EXPECT_FALSE (
        ambiguousChoice.next());
    EXPECT_EQ (
        splitter->nChans,
        0);
    EXPECT_FLOAT_EQ (
        splitter
            ->getDrawableSampleRate(),
        44100.0f);
    EXPECT_EQ (
        splitter
            ->lfpDisplay
            ->getNumChannels(),
        0);
    ASSERT_FALSE (
        splitter
            ->screenBufferIndex
            .isEmpty());
    EXPECT_EQ (
        splitter
            ->screenBufferIndex[0],
        0);
    ASSERT_FALSE (
        splitter
            ->lastScreenBufferIndex
            .isEmpty());
    EXPECT_EQ (
        splitter
            ->lastScreenBufferIndex[0],
        0);

    source->setStreamSourceNodeId (
        1,
        29);
    source->setStreamCountPreservingExisting (
        2,
        2);
    multiStreamTester
        ->updateSourceNodeSettings();
    const auto restoredBuffers =
        multiStreamProcessor
            ->getDisplayBuffers();
    ASSERT_EQ (
        restoredBuffers.size(),
        2);
    restoredBuffers[0]
        ->sampleRate =
        12345.0f;
    restoredBuffers[0]
        ->channelMetadata
        .getReference (0)
        .name =
        "RESTORED_A";
    restoredBuffers[0]
        ->channelMetadata
        .getReference (1)
        .name =
        "RESTORED_B";
    canvas->updateSettings();
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        "17|Shared");
    EXPECT_EQ (
        splitter
            ->displayBuffer,
        restoredBuffers[0]);
    EXPECT_EQ (
        splitter->nChans,
        2);
    EXPECT_FLOAT_EQ (
        splitter
            ->getDrawableSampleRate(),
        12345.0f);
    EXPECT_EQ (
        splitter
            ->lfpDisplay
            ->getNumChannels(),
        2);
    ASSERT_EQ (
        splitter
            ->lfpDisplay
            ->channels
            .size(),
        2);
    EXPECT_EQ (
        splitter
            ->lfpDisplay
            ->channels[0]
            ->getName(),
        "RESTORED_A");
}

TEST_F (LfpDisplayNodeTests,
        StreamChoiceIdsSurviveReorderRemovalZeroAndReaddition)
{
    auto dynamicTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    3 }),
            TestGuiRuntimeLifetime::
                process);
    auto* dynamicProcessor =
        dynamicTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto* source =
        dynamic_cast<
            FakeSourceNode*> (
            dynamicTester
                ->getSourceNode());
    ASSERT_NE (source, nullptr);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            dynamicProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (
        600,
        800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    auto* splitter =
        getDisplaySplitters (
            *canvas)[0];
    auto* selector =
        splitter
            ->streamSelection
            .get();
    auto retainedHandler =
        selector
            ->createAccessibilityHandler();
    ASSERT_NE (
        retainedHandler,
        nullptr);
    auto* retainedValue =
        retainedHandler
            ->getValueInterface();
    ASSERT_NE (
        retainedValue,
        nullptr);
    LfpThreadTrackingComboBoxListener
        listener;
    selector->addListener (
        &listener);
    const auto retainedKey =
        splitter
            ->selectedStreamKey;
    auto* const retainedBuffer =
        splitter
            ->displayBuffer;
    const auto id0 =
        selector
            ->getComponentID()
        + ".choice.2_fakesourcenode0_327c46616b65536f757263654e6f646530";
    const auto id1 =
        selector
            ->getComponentID()
        + ".choice.2_fakesourcenode1_327c46616b65536f757263654e6f646531";
    const auto id2 =
        selector
            ->getComponentID()
        + ".choice.2_fakesourcenode2_327c46616b65536f757263654e6f646532";
    const auto choiceIdForText =
        [&] (StringRef text)
    {
        for (PopupMenu::MenuItemIterator
                 iterator (
                     *selector
                          ->getRootMenu(),
                     false);
             iterator.next();)
        {
            if (iterator
                    .getItem()
                    .text
                == text)
            {
                return iterator
                    .getItem()
                    .accessibilityId;
            }
        }
        return String();
    };
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode0"),
        id0);
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode1"),
        id1);
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode2"),
        id2);

    selector->showPopup();
    ASSERT_TRUE (
        selector
            ->isPopupActive());
    source->moveStream (
        2,
        0);
    dynamicTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    EXPECT_FALSE (
        selector->isEnabled());
    EXPECT_EQ (
        selector->getText(),
        "Updating data streams");
    EXPECT_EQ (
        retainedValue
            ->getCurrentValueAsString(),
        "Updating data streams");
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        retainedKey);
    EXPECT_EQ (
        splitter
            ->displayBuffer,
        retainedBuffer);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);
    source->moveStream (
        0,
        2);
    dynamicTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    canvas->updateSettings();
    for (int attempt = 0;
         attempt < 100
             && (selector
                     ->isPopupActive()
                 || ! selector
                           ->isEnabled()
                 || choiceIdForText (
                        "FakeSourceNode0")
                        .isEmpty());
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_FALSE (
        selector
            ->isPopupActive());
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode0"),
        id0);
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode1"),
        id1);
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode2"),
        id2);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);
    source
        ->setStreamCountPreservingExisting (
            2,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode0"),
        id0);
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode1"),
        id1);
    EXPECT_TRUE (
        choiceIdForText (
            "FakeSourceNode2")
            .isEmpty());

    source
        ->setStreamCountPreservingExisting (
            3,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode2"),
        id2);
    for (auto* pane :
         getDisplaySplitters (
             *canvas))
    {
        canvas
            ->removeBufferForDisplay (
                pane->splitID,
                pane->displayBuffer);
    }

    source
        ->setStreamCountPreservingExisting (
            3,
            0);
    dynamicTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    EXPECT_EQ (
        selector
            ->getNumItems(),
        0);
    EXPECT_FALSE (
        selector->isEnabled());
    source
        ->setStreamCountPreservingExisting (
            3,
            4);
    dynamicTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    EXPECT_TRUE (
        selector->isEnabled());
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode0"),
        id0);
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode1"),
        id1);
    EXPECT_EQ (
        choiceIdForText (
            "FakeSourceNode2"),
        id2);
    selector->removeListener (
        &listener);
}

TEST_F (LfpDisplayNodeTests,
        DeferredStreamChoiceRebuildDoesNotOutliveCanvas)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (
        600,
        800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    auto* selector =
        getDisplaySplitters (
            *canvas)[0]
            ->streamSelection
            .get();
    auto retainedHandler =
        selector
            ->createAccessibilityHandler();
    ASSERT_NE (
        retainedHandler,
        nullptr);
    auto* retainedValue =
        retainedHandler
            ->getValueInterface();
    ASSERT_NE (
        retainedValue,
        nullptr);
    selector->showPopup();
    ASSERT_TRUE (
        selector
            ->isPopupActive());

    canvas->updateSettings();

    EXPECT_FALSE (
        selector->isEnabled());
    EXPECT_EQ (
        retainedValue
            ->getCurrentValueAsString(),
        "Updating data streams");
    canvas.reset();
    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            50);
    EXPECT_EQ (
        retainedValue
            ->getCurrentValueAsString(),
        "Updating data streams");
}

TEST_F (LfpDisplayNodeTests,
        ExposesEditableDisplayParametersForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    struct ParameterExpectation
    {
        String suffix;
        String title;
        String description;
        String initialValue;
    };
    const std::array<
        ParameterExpectation,
        3>
        parameters {
            ParameterExpectation {
                "timebase",
                "timebase",
                "Choose the time span shown in LFP display ",
                "2.0" },
            ParameterExpectation {
                "channel_height",
                "channel height",
                "Choose the channel height used in LFP display ",
                "40" },
            ParameterExpectation {
                "voltage_range",
                "voltage range",
                "Choose the voltage range shown in LFP display ",
                "250" }
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<
        std::array<Component*, 3>,
        3>
        controls {};

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        for (int parameterIndex = 0;
             parameterIndex
             < static_cast<int> (
                 parameters.size());
             ++parameterIndex)
        {
            const auto& expected =
                parameters[parameterIndex];
            const auto id =
                prefix
                + String (displayNumber)
                + "."
                + expected.suffix;
            auto* component =
                findLfpDescendantById (
                    *canvas,
                    id);
            controls[displayIndex]
                    [parameterIndex] =
                component;
            ASSERT_NE (
                component,
                nullptr)
                << id;
            auto* comboBox =
                dynamic_cast<
                    MessageThreadComboBox*> (
                    component);
            ASSERT_NE (
                comboBox,
                nullptr)
                << id;
            auto handler =
                comboBox
                    ->createAccessibilityHandler();
            ASSERT_NE (
                handler,
                nullptr);
            EXPECT_EQ (
                handler->getRole(),
                AccessibilityRole::
                    comboBox);
            EXPECT_EQ (
                handler->getTitle(),
                "LFP display "
                    + String (
                        displayNumber)
                    + " "
                    + expected.title);

            auto description =
                expected.description
                + String (
                    displayNumber);
            if (expected.suffix
                == "timebase")
            {
                description +=
                    ", in seconds.";
            }
            else if (expected.suffix
                     == "channel_height")
            {
                description +=
                    ", in pixels.";
            }
            else
            {
                description =
                    "Choose the DATA voltage range shown in LFP display "
                    + String (
                        displayNumber)
                    + ", in "
                    + String (
                        CharPointer_UTF8 (
                            "\xC2\xB5V"))
                    + ".";
            }
            EXPECT_EQ (
                handler
                    ->getDescription(),
                description);
            EXPECT_EQ (
                handler->getHelp(),
                description);
            auto* value =
                handler
                    ->getValueInterface();
            ASSERT_NE (
                value,
                nullptr);
            EXPECT_FALSE (
                value->isReadOnly());
            EXPECT_EQ (
                value
                    ->getCurrentValueAsString(),
                expected.initialValue);
            EXPECT_EQ (
                findLfpAncestor<
                    LfpViewer::
                        LfpDisplayOptions> (
                    *component)
                    ->isVisible(),
                displayIndex == 0);
        }
    }

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        for (int parameterIndex = 0;
             parameterIndex
             < static_cast<int> (
                 parameters.size());
             ++parameterIndex)
        {
            auto* control =
                controls[displayIndex]
                        [parameterIndex];
            EXPECT_EQ (
                findLfpAncestor<
                    LfpViewer::
                        LfpDisplayOptions> (
                    *control)
                    ->isVisible(),
                displayIndex == 0);
            EXPECT_EQ (
                control
                    ->getComponentID(),
                prefix
                    + String (
                        displayIndex + 1)
                    + "."
                    + parameters
                          [parameterIndex]
                              .suffix);
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesPauseToggleForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Component*, 3>
        pauseControls {};

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".pause";
        auto* component =
            findLfpDescendantById (
                *canvas,
                id);
        pauseControls[displayIndex] =
            component;
        ASSERT_NE (
            component,
            nullptr)
            << id;
        auto* handler =
            component
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                toggleButton);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " pause");
        const auto description =
            "Pause or resume live updates in LFP display "
            + String (
                displayNumber)
            + ". Acquisition and recording continue.";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_TRUE (
            handler->getCurrentState()
                .isCheckable());
        EXPECT_FALSE (
            handler->getCurrentState()
                .isChecked());
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Running");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *component)
                ->isVisible(),
            displayIndex == 0);
    }

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        ASSERT_NE (
            pauseControls[displayIndex],
            nullptr);
        EXPECT_EQ (
            pauseControls[displayIndex]
                ->getComponentID(),
            prefix
                + String (
                    displayIndex + 1)
                + ".pause");
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesEventOverlayLinesForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        for (int lineIndex = 0;
             lineIndex < 8;
             ++lineIndex)
        {
            const auto lineNumber =
                lineIndex + 1;
            const auto id =
                prefix
                + String (displayNumber)
                + ".event_overlay.line_"
                + String (lineNumber);
            auto* button =
                dynamic_cast<Button*> (
                    findLfpDescendantById (
                        *canvas,
                        id));
            ASSERT_NE (
                button,
                nullptr)
                << id;
            auto* handler =
                button
                    ->getAccessibilityHandler();
            ASSERT_NE (
                handler,
                nullptr);
            const auto description =
                "Show or hide TTL line "
                + String (lineNumber)
                + " event markers in LFP display "
                + String (displayNumber)
                + ". This changes the display overlay only; acquisition and recording are unaffected.";
            EXPECT_EQ (
                handler->getRole(),
                AccessibilityRole::
                    toggleButton);
            EXPECT_EQ (
                handler->getTitle(),
                "LFP display "
                    + String (
                        displayNumber)
                    + " event overlay line "
                    + String (
                        lineNumber));
            EXPECT_EQ (
                handler
                    ->getDescription(),
                description);
            EXPECT_EQ (
                handler->getHelp(),
                description);
            EXPECT_TRUE (
                handler
                    ->getCurrentState()
                    .isCheckable());
            EXPECT_TRUE (
                handler
                    ->getCurrentState()
                    .isChecked());
            EXPECT_TRUE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            toggle));
            EXPECT_FALSE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            press));
            auto* value =
                handler
                    ->getValueInterface();
            ASSERT_NE (
                value,
                nullptr);
            EXPECT_TRUE (
                value->isReadOnly());
            EXPECT_EQ (
                value
                    ->getCurrentValueAsString(),
                "Shown");
            EXPECT_EQ (
                findLfpAncestor<
                    LfpViewer::
                        LfpDisplayOptions> (
                    *button)
                    ->isVisible(),
                displayIndex == 0);
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        EventOverlayWorkerToggleIsPerPaneAndLifecycleSafe)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    auto* line1 =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "1.event_overlay.line_1"));
    auto* line2 =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "1.event_overlay.line_2"));
    auto* otherPaneLine1 =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "2.event_overlay.line_1"));
    ASSERT_NE (
        line1,
        nullptr);
    ASSERT_NE (
        line2,
        nullptr);
    ASSERT_NE (
        otherPaneLine1,
        nullptr);
    auto* handler =
        line1
            ->getAccessibilityHandler();
    auto* line2Handler =
        line2
            ->getAccessibilityHandler();
    auto* otherPaneHandler =
        otherPaneLine1
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    ASSERT_NE (
        line2Handler,
        nullptr);
    ASSERT_NE (
        otherPaneHandler,
        nullptr);
    auto* eventInterface =
        findLfpAncestor<
            LfpViewer::
                EventDisplayInterface> (
            *line1);
    ASSERT_NE (
        eventInterface,
        nullptr);

    const auto actions =
        handler->getActions();
    bool invoked = false;
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            invoked =
                actions.invoke (
                    AccessibilityActionType::
                        toggle);
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
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);

    EXPECT_TRUE (
        invoked);
    EXPECT_FALSE (
        eventInterface
            ->getEventDisplayState());
    EXPECT_FALSE (
        handler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Hidden");
    EXPECT_TRUE (
        line2Handler
            ->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        otherPaneHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_FALSE (
        line1->getToggleState());

    eventInterface
        ->setEventDisplayState (
            true);
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Shown");

    line1->triggerClick();
    for (int attempt = 0;
         attempt < 100
             && eventInterface
                    ->getEventDisplayState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_FALSE (
        eventInterface
            ->getEventDisplayState());
    EXPECT_FALSE (
        handler
            ->getCurrentState()
            .isChecked());

    line1->triggerClick();
    for (int attempt = 0;
         attempt < 100
             && ! eventInterface
                      ->getEventDisplayState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_TRUE (
        eventInterface
            ->getEventDisplayState());

    line1->setEnabled (
        false);
    EXPECT_FALSE (
        handler->isEnabled());
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                toggle));
    EXPECT_TRUE (
        eventInterface
            ->getEventDisplayState());

    line1->setEnabled (
        true);
    canvas.reset();

    workerReturned.store (
        false);
    std::thread staleWorker (
        [&]
        {
            actions.invoke (
                AccessibilityActionType::
                    toggle);
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
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        EventOverlayXmlMaskRoundTripsAndPreservesPaneIsolation)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    auto* pane1Line1 =
        findLfpDescendantById (
            *canvas,
            prefix
                + "1.event_overlay.line_1");
    auto* pane2Line1 =
        findLfpDescendantById (
            *canvas,
            prefix
                + "2.event_overlay.line_1");
    ASSERT_NE (
        pane1Line1,
        nullptr);
    ASSERT_NE (
        pane2Line1,
        nullptr);
    auto* pane1Options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *pane1Line1);
    auto* pane2Options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *pane2Line1);
    ASSERT_NE (
        pane1Options,
        nullptr);
    ASSERT_NE (
        pane2Options,
        nullptr);
    EXPECT_EQ (
        pane1Options
            ->getEventOverlayMask(),
        0xff);
    EXPECT_EQ (
        pane2Options
            ->getEventOverlayMask(),
        0xff);

    for (const auto mask :
         { 0x00,
           0x01,
           0x55,
           0x80,
           0xff })
    {
        XmlElement xml (
            "LFPDISPLAY");
        xml.setAttribute (
            "EventButtonState",
            mask);
        pane1Options
            ->restoreEventOverlayState (
                xml);
        EXPECT_EQ (
            pane1Options
                ->getEventOverlayMask(),
            mask);
        EXPECT_EQ (
            pane2Options
                ->getEventOverlayMask(),
            0xff);
        for (int lineIndex = 0;
             lineIndex < 8;
             ++lineIndex)
        {
            auto* button =
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + "1.event_overlay.line_"
                        + String (
                            lineIndex + 1));
            ASSERT_NE (
                button,
                nullptr);
            auto* handler =
                button
                    ->getAccessibilityHandler();
            ASSERT_NE (
                handler,
                nullptr);
            const auto expectedShown =
                ((mask >> lineIndex)
                 & 1)
                != 0;
            EXPECT_EQ (
                handler
                    ->getCurrentState()
                    .isChecked(),
                expectedShown);
            EXPECT_EQ (
                handler
                    ->getValueInterface()
                    ->getCurrentValueAsString(),
                expectedShown
                    ? "Shown"
                    : "Hidden");
        }

        XmlElement saved (
            "LFPDISPLAY");
        pane1Options
            ->saveEventOverlayState (
                saved);
        EXPECT_EQ (
            saved.getIntAttribute (
                "EventButtonState"),
            mask);
    }

    XmlElement missingAttribute (
        "LFPDISPLAY");
    pane1Options
        ->restoreEventOverlayState (
            missingAttribute);
    EXPECT_EQ (
        pane1Options
            ->getEventOverlayMask(),
        0x00);
    EXPECT_EQ (
        pane2Options
            ->getEventOverlayMask(),
        0xff);
}

TEST_F (LfpDisplayNodeTests,
        ExposesLatestNonzeroTtlWordForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".ttl_word";
        auto* component =
            findLfpDescendantById (
                *canvas,
                id);
        ASSERT_NE (
            component,
            nullptr)
            << id;
        auto* handler =
            component
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        const auto description =
            "Latest non-zero 64-bit TTL word received while its stream was selected in LFP display "
            + String (displayNumber)
            + ", shown as an unsigned decimal value. NONE means no non-zero word has been received by this display pane.";
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                staticText);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " latest non-zero TTL word");
        EXPECT_EQ (
            handler
                ->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "NONE");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *component)
                ->isVisible(),
            displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        TtlWordPublishingIsThreadSafeAndPaneLocal)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    auto* pane1Label =
        dynamic_cast<Label*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "1.ttl_word"));
    auto* pane2Label =
        dynamic_cast<Label*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "2.ttl_word"));
    ASSERT_NE (
        pane1Label,
        nullptr);
    ASSERT_NE (
        pane2Label,
        nullptr);
    auto* pane1Options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *pane1Label);
    ASSERT_NE (
        pane1Options,
        nullptr);
    auto* pane1Handler =
        pane1Label
            ->getAccessibilityHandler();
    auto* pane2Handler =
        pane2Label
            ->getAccessibilityHandler();
    ASSERT_NE (
        pane1Handler,
        nullptr);
    ASSERT_NE (
        pane2Handler,
        nullptr);
    auto* pane1Value =
        pane1Handler
            ->getValueInterface();
    ASSERT_NE (
        pane1Value,
        nullptr);

    std::atomic<bool>
        writerReturned { false };
    std::atomic<bool>
        readerReturned { false };
    std::atomic<bool>
        startWorkers { false };
    std::atomic<bool>
        publishingFinished { false };
    std::atomic<bool>
        readerSawInvalidValue {
            false
        };
    std::atomic<bool>
        readerSawPublishedValue {
            false
        };
    std::thread writer (
        [&]
        {
            while (! startWorkers.load())
                std::this_thread::yield();

            for (uint64 word = 1;
                 word <= 10000;
                 ++word)
            {
                pane1Options
                    ->setTTLWord (
                        word);
            }
            pane1Options
                ->setTTLWord (
                    0);
            writerReturned.store (
                true);
        });
    std::thread reader (
        [&]
        {
            while (! startWorkers.load())
                std::this_thread::yield();

            const auto inspectPublishedValue =
                [&]
            {
                const auto value =
                    pane1Value
                        ->getCurrentValueAsString();
                if (value != "NONE")
                {
                    readerSawPublishedValue
                        .store (true);
                    if (! value
                             .containsOnly (
                                 "0123456789"))
                    {
                        readerSawInvalidValue
                            .store (
                                true);
                    }
                }
            };
            while (! publishingFinished
                          .load())
            {
                inspectPublishedValue();
            }
            inspectPublishedValue();
            readerReturned.store (
                true);
        });
    startWorkers.store (true);
    while (! writerReturned.load())
    {
        pane1Options
            ->timerCallback();
        std::this_thread::yield();
    }
    pane1Options
        ->timerCallback();
    publishingFinished.store (
        true);
    writer.join();
    reader.join();
    EXPECT_TRUE (
        writerReturned.load());
    EXPECT_TRUE (
        readerReturned.load());
    EXPECT_FALSE (
        readerSawInvalidValue
            .load());
    EXPECT_TRUE (
        readerSawPublishedValue
            .load());
    EXPECT_EQ (
        pane1Label->getText(),
        "10000");
    EXPECT_EQ (
        pane1Value
            ->getCurrentValueAsString(),
        "10000");
    EXPECT_EQ (
        pane1Handler->getTitle(),
        "LFP display 1 latest non-zero TTL word");
    EXPECT_EQ (
        pane2Label->getText(),
        "NONE");
    EXPECT_EQ (
        pane2Handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "NONE");

    pane1Options
        ->setTTLWord (
            0);
    pane1Options
        ->timerCallback();
    EXPECT_EQ (
        pane1Value
            ->getCurrentValueAsString(),
        "10000");

    pane1Options
        ->setTTLWord (
            std::numeric_limits<
                uint64>::max());
    pane1Options
        ->timerCallback();
    EXPECT_EQ (
        pane1Label->getText(),
        "18446744073709551615");
    EXPECT_EQ (
        pane1Value
            ->getCurrentValueAsString(),
        "18446744073709551615");
    EXPECT_EQ (
        pane1Handler->getTitle(),
        "LFP display 1 latest non-zero TTL word");
}

TEST_F (LfpDisplayNodeTests,
        TtlWordFlowsThroughSerializedEventsAndRemainsTransient)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (
        1200,
        800);
    canvas->addToDesktop (
        0);
    canvas->setVisible (
        true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Label*, 3>
        labels {};
    std::array<
        LfpViewer::
            LfpDisplayOptions*,
        3>
        options {};
    std::array<
        LfpViewer::
            LfpDisplaySplitter*,
        3>
        splitters {};
    for (int paneIndex = 0;
         paneIndex < 3;
         ++paneIndex)
    {
        const auto paneNumber =
            paneIndex + 1;
        labels[paneIndex] =
            dynamic_cast<Label*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            paneNumber)
                        + ".ttl_word"));
        ASSERT_NE (
            labels[paneIndex],
            nullptr);
        options[paneIndex] =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *labels[paneIndex]);
        ASSERT_NE (
            options[paneIndex],
            nullptr);

        auto* selector =
            findLfpDescendantById (
                *canvas,
                prefix
                    + String (
                        paneNumber)
                    + ".stream");
        ASSERT_NE (
            selector,
            nullptr);
        splitters[paneIndex] =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplaySplitter> (
                *selector);
        ASSERT_NE (
            splitters[paneIndex],
            nullptr);
    }
    const auto streamId =
        processor
            ->getDataStreams()[0]
            ->getStreamId();
    const auto* sourceStream =
        tester
            ->getSourceNodeDataStream (
                streamId);
    ASSERT_NE (
        sourceStream,
        nullptr);
    const auto eventChannels =
        sourceStream
            ->getEventChannels();
    ASSERT_GE (
        eventChannels.size(),
        1);
    auto processEvent =
        [&] (TTLEventPtr event)
    {
        auto input =
            createBuffer (
                0.0f,
                1.0f,
                numChannels,
                8);
        tester->processBlock (
            processor,
            input,
            event.get());
        for (auto* paneOptions :
             options)
        {
            paneOptions
                ->timerCallback();
        }
    };

    splitters[1]
        ->selectedStreamId = 0;
    auto highBitEvent =
        TTLEvent::
            createTTLEvent (
                eventChannels[0],
                0,
                uint8 (63),
                true);
    processEvent (
        highBitEvent);

    EXPECT_EQ (
        labels[0]->getText(),
        "9223372036854775808");
    EXPECT_EQ (
        labels[1]->getText(),
        "NONE");
    EXPECT_EQ (
        labels[2]->getText(),
        "9223372036854775808");

    splitters[1]
        ->selectedStreamId =
        streamId;
    for (auto* paneOptions :
         options)
    {
        paneOptions
            ->timerCallback();
    }
    EXPECT_EQ (
        labels[1]->getText(),
        "NONE");

    XmlElement before (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &before);
    auto lowBitEvent =
        TTLEvent::
            createTTLEvent (
                eventChannels[0],
                8,
                uint8 (0),
                true);
    processEvent (
        lowBitEvent);
    for (auto* label :
         labels)
    {
        EXPECT_EQ (
            label->getText(),
            "9223372036854775809");
        auto* handler =
            label
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "9223372036854775809");
    }

    auto clearHighBitEvent =
        TTLEvent::
            createTTLEvent (
                eventChannels[0],
                16,
                uint8 (63),
                false);
    processEvent (
        clearHighBitEvent);
    for (auto* label :
         labels)
    {
        EXPECT_EQ (
            label->getText(),
            "1");
    }

    auto zeroWordEvent =
        TTLEvent::
            createTTLEvent (
                eventChannels[0],
                24,
                uint8 (0),
                false);
    ASSERT_EQ (
        zeroWordEvent
            ->getWord(),
        0);
    processEvent (
        zeroWordEvent);
    for (auto* label :
         labels)
    {
        EXPECT_EQ (
            label->getText(),
            "1");
    }

    XmlElement after (
        "ROOT");
    canvas
        ->saveCustomParametersToXml (
            &after);
    EXPECT_TRUE (
        before
            .isEquivalentTo (
                &after,
                true));
}

TEST_F (LfpDisplayNodeTests,
        ExposesOptionsDrawerForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".options_drawer";
        auto* button =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            button,
            nullptr)
            << id;
        auto* handler =
            button
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                button);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " options drawer");
        const auto description =
            "Show or hide advanced options for all LFP displays. Opening the drawer reveals threshold, channel, signal-processing, and triggered-display controls.";
        EXPECT_EQ (
            handler
                ->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_FALSE (
            button->getToggleState());
        EXPECT_FALSE (
            handler
                ->getCurrentState()
                .isCheckable());
        EXPECT_TRUE (
            handler
                ->getCurrentState()
                .isExpandable());
        EXPECT_TRUE (
            handler
                ->getCurrentState()
                .isCollapsed());
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Closed");
        for (const auto action :
             { AccessibilityActionType::
                   press,
               AccessibilityActionType::
                   expand,
               AccessibilityActionType::
                   collapse })
        {
            EXPECT_TRUE (
                handler->getActions()
                    .contains (
                        action));
        }
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *button)
                ->isVisible(),
            displayIndex == 0);
    }

    canvas->setLayout (
        LfpViewer::
            SplitLayouts::
                THREE_HORZ);
    int visibleOptionsCount = 0;
    for (int displayNumber = 1;
         displayNumber <= 3;
         ++displayNumber)
    {
        auto* button =
            findLfpDescendantById (
                *canvas,
                prefix
                    + String (
                        displayNumber)
                    + ".options_drawer");
        ASSERT_NE (
            button,
            nullptr);
        if (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *button)
                ->isVisible())
        {
            ++visibleOptionsCount;
        }
    }
    EXPECT_EQ (
        visibleOptionsCount,
        1);
}

TEST_F (LfpDisplayNodeTests,
        OptionsDrawerWorkerActionsSynchroniseEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3>
        buttons {};
    std::array<
        AccessibilityHandler*,
        3>
        handlers {};
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        buttons[displayIndex] =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            displayIndex
                            + 1)
                        + ".options_drawer"));
        ASSERT_NE (
            buttons[displayIndex],
            nullptr);
        handlers[displayIndex] =
            buttons[displayIndex]
                ->getAccessibilityHandler();
        ASSERT_NE (
            handlers[displayIndex],
            nullptr);
    }

    LfpThreadTrackingButtonListener
        listener;
    buttons[0]->addListener (
        &listener);
    const auto invokeFromWorker =
        [&] (
            AccessibilityActionType
                action)
    {
        bool invoked = false;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                invoked =
                    handlers[0]
                        ->getActions()
                        .invoke (
                            action);
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
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        EXPECT_TRUE (
            invoked);
    };

    invokeFromWorker (
        AccessibilityActionType::
            expand);
    EXPECT_TRUE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_TRUE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isExpanded());
        EXPECT_EQ (
            handlers[displayIndex]
                ->getValueInterface()
                ->getCurrentValueAsString(),
            "Open");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getHeight(),
            210);
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getExtendedOptionsHeight(),
            150);
    }

    invokeFromWorker (
        AccessibilityActionType::
            expand);
    EXPECT_TRUE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);

    invokeFromWorker (
        AccessibilityActionType::
            collapse);
    EXPECT_FALSE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_FALSE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isCollapsed());
        EXPECT_EQ (
            handlers[displayIndex]
                ->getValueInterface()
                ->getCurrentValueAsString(),
            "Closed");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getHeight(),
            60);
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getExtendedOptionsHeight(),
            0);
    }

    invokeFromWorker (
        AccessibilityActionType::
            collapse);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);

    canvas
        ->toggleOptionsDrawer (
            true);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isExpanded());
    }

    buttons[0]->removeListener (
        &listener);
}

TEST_F (LfpDisplayNodeTests,
        OptionsDrawerActionsIgnoreDisabledAndDestroyedControls)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.options_drawer";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        button,
        nullptr);
    auto* handler =
        button
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    const auto actions =
        handler->getActions();

    button->setEnabled (
        false);
    EXPECT_FALSE (
        handler->isEnabled());
    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                expand));
    EXPECT_FALSE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_FALSE (
        button->getToggleState());

    button->setEnabled (
        true);
    canvas.reset();

    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            actions.invoke (
                AccessibilityActionType::
                    expand);
            actions.invoke (
                AccessibilityActionType::
                    press);
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
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        OptionsDrawerXmlStateRoundTripsWithoutNotifications)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3>
        buttons {};
    std::array<
        AccessibilityHandler*,
        3>
        handlers {};
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        buttons[displayIndex] =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            displayIndex
                            + 1)
                        + ".options_drawer"));
        ASSERT_NE (
            buttons[displayIndex],
            nullptr);
        handlers[displayIndex] =
            buttons[displayIndex]
                ->getAccessibilityHandler();
        ASSERT_NE (
            handlers[displayIndex],
            nullptr);
    }

    LfpThreadTrackingButtonListener
        listener;
    buttons[0]->addListener (
        &listener);
    canvas
        ->toggleOptionsDrawer (
            true);
    XmlElement savedNode (
        "CANVAS");
    canvas
        ->saveOptionsDrawerState (
            savedNode);
    EXPECT_TRUE (
        savedNode.hasAttribute (
            "showAllOptions"));
    EXPECT_TRUE (
        savedNode.getBoolAttribute (
            "showAllOptions"));

    canvas
        ->toggleOptionsDrawer (
            false);
    canvas
        ->restoreOptionsDrawerState (
            savedNode);
    EXPECT_TRUE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_TRUE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isExpanded());
        auto* options =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex]);
        ASSERT_NE (
            options,
            nullptr);
        EXPECT_EQ (
            options->getHeight(),
            210);
        EXPECT_EQ (
            options
                ->getExtendedOptionsHeight(),
            150);
    }

    XmlElement explicitClosedNode (
        "CANVAS");
    explicitClosedNode.setAttribute (
        "showAllOptions",
        false);
    canvas
        ->restoreOptionsDrawerState (
            explicitClosedNode);
    EXPECT_FALSE (
        canvas
            ->optionsDrawerIsOpen);

    canvas
        ->toggleOptionsDrawer (
            true);
    XmlElement missingAttributeNode (
        "CANVAS");
    canvas
        ->restoreOptionsDrawerState (
            missingAttributeNode);
    EXPECT_FALSE (
        canvas
            ->optionsDrawerIsOpen);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_FALSE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_TRUE (
            handlers[displayIndex]
                ->getCurrentState()
                .isCollapsed());
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex])
                ->getExtendedOptionsHeight(),
            0);
    }
    buttons[0]->removeListener (
        &listener);
}

TEST_F (LfpDisplayNodeTests,
        ExposesReverseOrderToggleForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (
        true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".reverse_order";
        auto* button =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            button,
            nullptr)
            << id;
        auto* handler =
            button
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        const auto description =
            "Reverse the current visible channel order in LFP display "
            + String (displayNumber)
            + " after filtering, channel skipping, and optional metadata-based depth sorting. This changes display order only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                toggleButton);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " reverse channel order");
        EXPECT_EQ (
            handler
                ->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_TRUE (
            handler
                ->getCurrentState()
                .isCheckable());
        EXPECT_FALSE (
            handler
                ->getCurrentState()
                .isChecked());
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Off");
        auto* paneOptions =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *button);
        ASSERT_NE (
            paneOptions,
            nullptr);
        std::vector<Label*> labels;
        collectLfpDescendants (
            *paneOptions,
            labels);
        const auto labelIterator =
            std::find_if (
                labels.begin(),
                labels.end(),
                [] (const Label* candidate)
                {
                    return candidate->getName()
                           == "ReverseChannelsLabel";
                });
        ASSERT_NE (
            labelIterator,
            labels.end());
        auto* label = *labelIterator;
        ASSERT_NE (
            label,
            nullptr);
        EXPECT_FALSE (
            label->isAccessible());
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *button)
                ->isVisible(),
            displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesInvertSignalToggleForEveryPane)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        const auto displayNumber = displayIndex + 1;
        const auto id = prefix + String (displayNumber)
                        + ".invert_signal";
        auto* button = dynamic_cast<Button*> (
            findLfpDescendantById (*canvas, id));
        ASSERT_NE (button, nullptr) << id;
        EXPECT_NE (dynamic_cast<LfpViewer::LfpOptionToggleButton*> (button),
                   nullptr);
        auto* handler = button->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        const auto description =
            "Set displayed signal polarity to inverted or normal for all invertible channels in LFP display "
            + String (displayNumber)
            + ". This changes visualization only; acquisition and recording data are unaffected.";
        EXPECT_EQ (handler->getRole(), AccessibilityRole::toggleButton);
        EXPECT_EQ (handler->getTitle(),
                   "LFP display " + String (displayNumber)
                       + " invert signal polarity");
        EXPECT_EQ (handler->getDescription(), description);
        EXPECT_EQ (handler->getHelp(), description);
        EXPECT_TRUE (handler->getCurrentState().isCheckable());
        EXPECT_FALSE (handler->getCurrentState().isChecked());
        EXPECT_TRUE (handler->getActions().contains (
            AccessibilityActionType::toggle));
        EXPECT_FALSE (handler->getActions().contains (
            AccessibilityActionType::press));
        auto* value = handler->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_TRUE (value->isReadOnly());
        EXPECT_EQ (value->getCurrentValueAsString(), "Off");
        auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
        ASSERT_NE (options, nullptr);
        std::vector<Label*> labels;
        collectLfpDescendants (*options, labels);
        const auto label = std::find_if (
            labels.begin(), labels.end(), [] (const Label* candidate)
            { return candidate->getName() == "InvertInputLabel"; });
        ASSERT_NE (label, labels.end());
        EXPECT_FALSE ((*label)->isAccessible());
        EXPECT_EQ (options->isVisible(), displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesShowChannelNumbersToggleForEveryPane)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        const auto displayNumber = displayIndex + 1;
        const auto id = prefix + String (displayNumber)
                        + ".show_channel_numbers";
        auto* button = dynamic_cast<Button*> (
            findLfpDescendantById (*canvas, id));
        ASSERT_NE (button, nullptr) << id;
        EXPECT_NE (dynamic_cast<LfpViewer::LfpOptionToggleButton*> (button),
                   nullptr);
        auto* handler = button->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        const auto description =
            "Show channel numbers instead of channel names in LFP display "
            + String (displayNumber)
            + ". This changes labels and tooltips only; acquisition and recording are unaffected.";
        EXPECT_EQ (handler->getRole(), AccessibilityRole::toggleButton);
        EXPECT_EQ (handler->getTitle(),
                   "LFP display " + String (displayNumber)
                       + " show channel numbers");
        EXPECT_EQ (handler->getDescription(), description);
        EXPECT_EQ (handler->getHelp(), description);
        EXPECT_TRUE (handler->getCurrentState().isCheckable());
        EXPECT_FALSE (handler->getCurrentState().isChecked());
        EXPECT_TRUE (handler->getActions().contains (
            AccessibilityActionType::toggle));
        EXPECT_FALSE (handler->getActions().contains (
            AccessibilityActionType::press));
        auto* value = handler->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_TRUE (value->isReadOnly());
        EXPECT_EQ (value->getCurrentValueAsString(), "Off");
        auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
        ASSERT_NE (options, nullptr);
        std::vector<Label*> labels;
        collectLfpDescendants (*options, labels);
        const auto label = std::find_if (
            labels.begin(), labels.end(), [] (const Label* candidate)
            { return candidate->getName() == "ShowChannelNumberLabel"; });
        ASSERT_NE (label, labels.end());
        EXPECT_FALSE ((*label)->isAccessible());
        EXPECT_EQ (options->isVisible(), displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesSubtractOffsetToggleForEveryPane)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        const auto displayNumber = displayIndex + 1;
        const auto id = prefix + String (displayNumber) + ".subtract_offset";
        auto* button = dynamic_cast<Button*> (
            findLfpDescendantById (*canvas, id));
        ASSERT_NE (button, nullptr) << id;
        EXPECT_NE (dynamic_cast<LfpViewer::LfpOptionToggleButton*> (button),
                   nullptr);
        auto* handler = button->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        const auto description =
            "Subtract each channel's display offset, computed from its current screen-buffer mean, in LFP display "
            + String (displayNumber)
            + ". Spike raster plotting requires this option and keeps it on. This affects display rendering only; acquisition and recording are unaffected.";
        EXPECT_EQ (handler->getRole(), AccessibilityRole::toggleButton);
        EXPECT_EQ (handler->getTitle(),
                   "LFP display " + String (displayNumber)
                       + " subtract offset");
        EXPECT_EQ (handler->getDescription(), description);
        EXPECT_EQ (handler->getHelp(), description);
        EXPECT_TRUE (handler->getCurrentState().isCheckable());
        EXPECT_FALSE (handler->getCurrentState().isChecked());
        EXPECT_TRUE (handler->getActions().contains (
            AccessibilityActionType::toggle));
        EXPECT_FALSE (handler->getActions().contains (
            AccessibilityActionType::press));
        auto* value = handler->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_TRUE (value->isReadOnly());
        EXPECT_EQ (value->getCurrentValueAsString(), "Off");
        auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
        ASSERT_NE (options, nullptr);
        std::vector<Label*> labels;
        collectLfpDescendants (*options, labels);
        const auto label = std::find_if (
            labels.begin(), labels.end(), [] (const Label* candidate)
            { return candidate->getName() == "MedianOffsetPlottingLabel"; });
        ASSERT_NE (label, labels.end());
        EXPECT_FALSE ((*label)->isAccessible());
        EXPECT_EQ (options->isVisible(), displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        ShowChannelNumbersToggleRunsOnMessageThreadAndSurvivesEdgeCases)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id = "oe.processor." + String (processor->getNodeId())
                    + ".lfp.display_1.show_channel_numbers";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    ASSERT_NE (button, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    auto* handler = button->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    const auto actions = handler->getActions();

    LfpThreadTrackingButtonListener listener;
    button->addListener (&listener);
    std::atomic<bool> workerReturned { false };
    bool toggled = false;
    std::thread worker ([&]
    {
        toggled = actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100
                        && (! workerReturned.load()
                            || ! options->getChannelNameState());
         ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (worker, workerReturned);
    button->removeListener (&listener);
    EXPECT_TRUE (toggled);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
    EXPECT_TRUE (options->getChannelNameState());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");

    options->setShowChannelNumbers (false);
    button->setEnabled (false);
    workerReturned.store (false);
    std::thread disabledWorker ([&]
    {
        toggled = actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (disabledWorker, workerReturned);
    EXPECT_TRUE (toggled);
    EXPECT_FALSE (options->getChannelNameState());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_FALSE (handler->isEnabled());
    button->setEnabled (true);

    auto zeroChannelTester = std::make_unique<ProcessorTester> (
        TestSourceNodeBuilder (FakeSourceNodeParams { 0, sampleRate, bitVolts }));
    auto* zeroChannelProcessor = zeroChannelTester->createProcessor<LfpViewer::LfpDisplayNode> (
        Plugin::Processor::SINK);
    auto zeroCanvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        zeroChannelProcessor, LfpViewer::SplitLayouts::SINGLE, false);
    zeroCanvas->updateSettings();
    zeroCanvas->setSize (900, 800);
    zeroCanvas->addToDesktop (0);
    zeroCanvas->setVisible (true);
    zeroCanvas->toggleOptionsDrawer (true);
    const auto zeroId = "oe.processor." + String (zeroChannelProcessor->getNodeId())
                        + ".lfp.display_1.show_channel_numbers";
    auto* zeroButton = dynamic_cast<Button*> (
        findLfpDescendantById (*zeroCanvas, zeroId));
    ASSERT_NE (zeroButton, nullptr);
    auto* zeroOptions = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*zeroButton);
    ASSERT_NE (zeroOptions, nullptr);
    EXPECT_TRUE (zeroButton->getAccessibilityHandler()->getActions().invoke (
        AccessibilityActionType::toggle));
    EXPECT_TRUE (zeroOptions->getChannelNameState());
    EXPECT_TRUE (zeroButton->getToggleState());

    canvas.reset();
    workerReturned.store (false);
    std::thread destroyedWorker ([&]
    {
        actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (destroyedWorker, workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        SubtractOffsetToggleRunsOnMessageThreadAndSurvivesEdgeCases)
{
    auto input = createBuffer (0.0f, 1.0f, numChannels, 128);
    writeBlock (input);
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->updateSettings();
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id = "oe.processor." + String (processor->getNodeId())
                    + ".lfp.display_1.subtract_offset";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (button, nullptr);
    ASSERT_NE (display, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    auto* handler = button->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    const auto actions = handler->getActions();

    LfpThreadTrackingButtonListener listener;
    button->addListener (&listener);
    std::atomic<bool> workerReturned { false };
    bool toggled = false;
    std::thread worker ([&]
    {
        toggled = actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100
                        && (! workerReturned.load()
                            || ! display->getMedianOffsetPlotting());
         ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (worker, workerReturned);
    button->removeListener (&listener);
    EXPECT_TRUE (toggled);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");

    options->setMedianOffset (false);
    button->setEnabled (false);
    workerReturned.store (false);
    std::thread disabledWorker ([&]
    {
        toggled = actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (disabledWorker, workerReturned);
    EXPECT_TRUE (toggled);
    EXPECT_FALSE (display->getMedianOffsetPlotting());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_FALSE (handler->isEnabled());
    button->setEnabled (true);

    auto zeroChannelTester = std::make_unique<ProcessorTester> (
        TestSourceNodeBuilder (FakeSourceNodeParams { 0, sampleRate, bitVolts }));
    auto* zeroChannelProcessor = zeroChannelTester->createProcessor<LfpViewer::LfpDisplayNode> (
        Plugin::Processor::SINK);
    auto zeroCanvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        zeroChannelProcessor, LfpViewer::SplitLayouts::SINGLE, false);
    zeroCanvas->updateSettings();
    zeroCanvas->setSize (900, 800);
    zeroCanvas->addToDesktop (0);
    zeroCanvas->setVisible (true);
    zeroCanvas->toggleOptionsDrawer (true);
    zeroCanvas->removeBufferForDisplay (0);
    const auto zeroId = "oe.processor." + String (zeroChannelProcessor->getNodeId())
                        + ".lfp.display_1.subtract_offset";
    auto* zeroButton = dynamic_cast<Button*> (
        findLfpDescendantById (*zeroCanvas, zeroId));
    auto* zeroDisplay = findLfpDescendant<LfpViewer::LfpDisplay> (*zeroCanvas);
    ASSERT_NE (zeroButton, nullptr);
    ASSERT_NE (zeroDisplay, nullptr);
    EXPECT_TRUE (zeroButton->getAccessibilityHandler()->getActions().invoke (
        AccessibilityActionType::toggle));
    EXPECT_TRUE (zeroDisplay->getMedianOffsetPlotting());

    canvas.reset();
    workerReturned.store (false);
    std::thread destroyedWorker ([&]
    {
        actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (destroyedWorker, workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        ShowChannelNumbersXmlRoundTripsPerPaneWithoutNotifications)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    std::array<Button*, 3> buttons {};
    std::array<LfpViewer::LfpDisplayOptions*, 3> options {};
    std::array<AccessibilityValueInterface*, 3> values {};
    std::array<LfpThreadTrackingButtonListener, 3> listeners;
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        buttons[displayIndex] = dynamic_cast<Button*> (findLfpDescendantById (
            *canvas, prefix + String (displayIndex + 1)
                         + ".show_channel_numbers"));
        ASSERT_NE (buttons[displayIndex], nullptr);
        options[displayIndex] = findLfpAncestor<LfpViewer::LfpDisplayOptions> (
            *buttons[displayIndex]);
        ASSERT_NE (options[displayIndex], nullptr);
        values[displayIndex] = buttons[displayIndex]->getAccessibilityHandler()
                                   ->getValueInterface();
        ASSERT_NE (values[displayIndex], nullptr);
        buttons[displayIndex]->addListener (&listeners[displayIndex]);
        options[displayIndex]->setShowChannelNumbers (displayIndex != 1);
    }

    XmlElement savedRoot ("ROOT");
    for (auto* paneOptions : options)
        paneOptions->saveParameters (&savedRoot);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        auto* savedPane = savedRoot.getChildByName (
            "LFPDISPLAY" + String (displayIndex));
        ASSERT_NE (savedPane, nullptr);
        EXPECT_EQ (savedPane->getBoolAttribute ("showChannelNum"),
                   displayIndex != 1);
        options[displayIndex]->setShowChannelNumbers (false);
    }
    for (auto* paneOptions : options)
        paneOptions->loadParameters (&savedRoot);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        const auto expected = displayIndex != 1;
        EXPECT_EQ (options[displayIndex]->getChannelNameState(), expected);
        EXPECT_EQ (buttons[displayIndex]->getToggleState(), expected);
        EXPECT_EQ (buttons[displayIndex]->getAccessibilityHandler()
                       ->getCurrentState().isChecked(), expected);
        EXPECT_EQ (values[displayIndex]->getCurrentValueAsString(),
                   expected ? String ("On") : String ("Off"));
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
    }
    for (auto* savedPane : savedRoot.getChildIterator())
        savedPane->removeAttribute ("showChannelNum");
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        options[displayIndex]->setShowChannelNumbers (true);
        options[displayIndex]->loadParameters (&savedRoot);
        EXPECT_FALSE (options[displayIndex]->getChannelNameState());
        EXPECT_FALSE (buttons[displayIndex]->getToggleState());
        EXPECT_FALSE (buttons[displayIndex]->getAccessibilityHandler()
                          ->getCurrentState().isChecked());
        EXPECT_EQ (values[displayIndex]->getCurrentValueAsString(), "Off");
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
        buttons[displayIndex]->removeListener (&listeners[displayIndex]);
    }
}

TEST_F (LfpDisplayNodeTests,
        SubtractOffsetXmlRoundTripsPerPaneWithoutNotifications)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    std::array<Button*, 3> buttons {};
    std::array<LfpViewer::LfpDisplayOptions*, 3> options {};
    std::array<AccessibilityValueInterface*, 3> values {};
    std::array<LfpThreadTrackingButtonListener, 3> listeners;
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        buttons[displayIndex] = dynamic_cast<Button*> (findLfpDescendantById (
            *canvas, prefix + String (displayIndex + 1) + ".subtract_offset"));
        ASSERT_NE (buttons[displayIndex], nullptr);
        options[displayIndex] = findLfpAncestor<LfpViewer::LfpDisplayOptions> (
            *buttons[displayIndex]);
        ASSERT_NE (options[displayIndex], nullptr);
        values[displayIndex] = buttons[displayIndex]->getAccessibilityHandler()
                                   ->getValueInterface();
        ASSERT_NE (values[displayIndex], nullptr);
        buttons[displayIndex]->addListener (&listeners[displayIndex]);
        options[displayIndex]->setMedianOffset (displayIndex != 1);
    }

    XmlElement savedRoot ("ROOT");
    for (auto* paneOptions : options)
        paneOptions->saveParameters (&savedRoot);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        auto* savedPane = savedRoot.getChildByName (
            "LFPDISPLAY" + String (displayIndex));
        ASSERT_NE (savedPane, nullptr);
        EXPECT_EQ (savedPane->getBoolAttribute ("subtractOffset"),
                   displayIndex != 1);
        options[displayIndex]->setMedianOffset (false);
    }
    for (auto* paneOptions : options)
        paneOptions->loadParameters (&savedRoot);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        const auto expected = displayIndex != 1;
        EXPECT_EQ (buttons[displayIndex]->getToggleState(), expected);
        EXPECT_EQ (buttons[displayIndex]->getAccessibilityHandler()
                       ->getCurrentState().isChecked(), expected);
        EXPECT_EQ (values[displayIndex]->getCurrentValueAsString(),
                   expected ? String ("On") : String ("Off"));
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
    }
    for (auto* savedPane : savedRoot.getChildIterator())
        savedPane->removeAttribute ("subtractOffset");
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        options[displayIndex]->setMedianOffset (true);
        options[displayIndex]->loadParameters (&savedRoot);
        EXPECT_FALSE (buttons[displayIndex]->getToggleState());
        EXPECT_FALSE (buttons[displayIndex]->getAccessibilityHandler()
                          ->getCurrentState().isChecked());
        EXPECT_EQ (values[displayIndex]->getCurrentValueAsString(), "Off");
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
        buttons[displayIndex]->removeListener (&listeners[displayIndex]);
    }
}

TEST_F (LfpDisplayNodeTests,
        SpikeRasterOwnsAndRestoresSubtractOffsetThroughItsComboBox)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->resized();
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    processor->startAcquisition();
    canvas->beginAnimation();
    auto input = createBuffer (0.0f, 1.0f, numChannels, 128);
    writeBlock (input);
    canvas->refreshState();
    canvas->toggleOptionsDrawer (true);

    const auto id = "oe.processor." + String (processor->getNodeId())
                    + ".lfp.display_1.subtract_offset";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (button, nullptr);
    ASSERT_NE (display, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    std::vector<ComboBox*> comboBoxes;
    collectLfpDescendants (*options, comboBoxes);
    const auto spikeRaster = std::find_if (
        comboBoxes.begin(), comboBoxes.end(), [] (const ComboBox* candidate)
        { return candidate->getName() == "spikeRasterSelection"; });
    ASSERT_NE (spikeRaster, comboBoxes.end());
    auto* handler = button->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);

    LfpThreadTrackingButtonListener listener;
    button->addListener (&listener);
    (*spikeRaster)->setSelectedId (2, sendNotificationSync);
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");
    const auto actions = handler->getActions();
    EXPECT_TRUE (actions.invoke (AccessibilityActionType::toggle));
    EXPECT_EQ (listener.callbackCount.load(), 2);
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (display->getMedianOffsetPlotting());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_EQ (value->getCurrentValueAsString(), "Off");

    options->setMedianOffset (true);
    (*spikeRaster)->setSelectedId (2, sendNotificationSync);
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");
    button->removeListener (&listener);
    processor->stopAcquisition();
}

TEST_F (LfpDisplayNodeTests,
        SpikeRasterComboBoxRemainsCoherentWithoutChannelsOrDisplayBuffer)
{
    auto zeroChannelTester = std::make_unique<ProcessorTester> (
        TestSourceNodeBuilder (FakeSourceNodeParams { 0, sampleRate, bitVolts }));
    auto* zeroChannelProcessor = zeroChannelTester->createProcessor<LfpViewer::LfpDisplayNode> (
        Plugin::Processor::SINK);
    auto zeroCanvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        zeroChannelProcessor, LfpViewer::SplitLayouts::SINGLE, false);
    zeroCanvas->updateSettings();
    zeroCanvas->setSize (900, 800);
    zeroCanvas->addToDesktop (0);
    zeroCanvas->setVisible (true);
    zeroCanvas->toggleOptionsDrawer (true);

    auto* zeroDisplay = findLfpDescendant<LfpViewer::LfpDisplay> (*zeroCanvas);
    ASSERT_NE (zeroDisplay, nullptr);
    ASSERT_EQ (zeroChannelProcessor->getNumInputs(), 0);
    std::vector<ComboBox*> zeroComboBoxes;
    collectLfpDescendants (*zeroCanvas, zeroComboBoxes);
    const auto zeroSpikeRaster = std::find_if (
        zeroComboBoxes.begin(), zeroComboBoxes.end(), [] (const ComboBox* candidate)
        { return candidate->getName() == "spikeRasterSelection"; });
    const auto zeroTimebase = std::find_if (
        zeroComboBoxes.begin(), zeroComboBoxes.end(), [] (const ComboBox* candidate)
        { return candidate->getName() == "Timebase"; });
    ASSERT_NE (zeroSpikeRaster, zeroComboBoxes.end());
    ASSERT_NE (zeroTimebase, zeroComboBoxes.end());

    (*zeroSpikeRaster)->setSelectedId (3, sendNotificationSync);
    EXPECT_TRUE (zeroDisplay->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (zeroDisplay->getSpikeRasterThreshold(), -100.0f);
    EXPECT_TRUE (zeroDisplay->getMedianOffsetPlotting());
    (*zeroSpikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (zeroDisplay->getSpikeRasterPlotting());
    EXPECT_FALSE (zeroDisplay->getMedianOffsetPlotting());

    (*zeroSpikeRaster)->setText ("123.5", sendNotificationSync);
    EXPECT_EQ ((*zeroSpikeRaster)->getText(), "-123.5");
    EXPECT_TRUE (zeroDisplay->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (zeroDisplay->getSpikeRasterThreshold(), -123.5f);
    (*zeroSpikeRaster)->setText ("999", sendNotificationSync);
    EXPECT_EQ ((*zeroSpikeRaster)->getText(), "-500");
    EXPECT_FLOAT_EQ (zeroDisplay->getSpikeRasterThreshold(), -500.0f);
    (*zeroSpikeRaster)->setText ("0", sendNotificationSync);
    EXPECT_EQ ((*zeroSpikeRaster)->getText(), "OFF");
    EXPECT_FALSE (zeroDisplay->getSpikeRasterPlotting());
    EXPECT_FALSE (zeroDisplay->getMedianOffsetPlotting());
    (*zeroSpikeRaster)->setText ("not a number", sendNotificationSync);
    EXPECT_EQ ((*zeroSpikeRaster)->getText(), "OFF");
    EXPECT_FALSE (zeroDisplay->getSpikeRasterPlotting());

    auto* zeroOptions = findLfpAncestor<LfpViewer::LfpDisplayOptions> (
        **zeroSpikeRaster);
    ASSERT_NE (zeroOptions, nullptr);
    zeroOptions->setMedianOffset (true);
    (*zeroSpikeRaster)->setSelectedId (2, sendNotificationSync);
    (*zeroSpikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (zeroDisplay->getSpikeRasterPlotting());
    EXPECT_TRUE (zeroDisplay->getMedianOffsetPlotting());

    auto* zeroSplitter = findLfpDescendant<LfpViewer::LfpDisplaySplitter> (
        *zeroCanvas);
    ASSERT_NE (zeroSplitter, nullptr);
    const auto initialTimebase = zeroSplitter->timebase;
    (*zeroTimebase)->setSelectedId (7, sendNotificationSync);
    EXPECT_EQ ((*zeroTimebase)->getSelectedId(), 7);
    EXPECT_FLOAT_EQ (zeroSplitter->timebase, initialTimebase);

    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    canvas->removeBufferForDisplay (0);

    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (display, nullptr);
    std::vector<ComboBox*> comboBoxes;
    collectLfpDescendants (*canvas, comboBoxes);
    const auto spikeRaster = std::find_if (
        comboBoxes.begin(), comboBoxes.end(), [] (const ComboBox* candidate)
        { return candidate->getName() == "spikeRasterSelection"; });
    ASSERT_NE (spikeRaster, comboBoxes.end());

    (*spikeRaster)->setSelectedId (4, sendNotificationSync);
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -150.0f);
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    (*spikeRaster)->setText ("234", sendNotificationSync);
    EXPECT_EQ ((*spikeRaster)->getText(), "-234");
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -234.0f);
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_FALSE (display->getMedianOffsetPlotting());
}

TEST_F (LfpDisplayNodeTests,
        SpikeRasterXmlRestoresSubtractOffsetOwnershipWithoutNotifications)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->resized();
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    processor->startAcquisition();
    canvas->beginAnimation();
    auto input = createBuffer (0.0f, 1.0f, numChannels, 128);
    writeBlock (input);
    canvas->refreshState();
    canvas->toggleOptionsDrawer (true);

    const auto id = "oe.processor." + String (processor->getNodeId())
                    + ".lfp.display_1.subtract_offset";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (button, nullptr);
    ASSERT_NE (display, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    std::vector<ComboBox*> comboBoxes;
    collectLfpDescendants (*options, comboBoxes);
    const auto spikeRaster = std::find_if (
        comboBoxes.begin(), comboBoxes.end(), [] (const ComboBox* candidate)
        { return candidate->getName() == "spikeRasterSelection"; });
    ASSERT_NE (spikeRaster, comboBoxes.end());
    auto* handler = button->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);

    LfpThreadTrackingButtonListener listener;
    button->addListener (&listener);
    options->setMedianOffset (false);

    XmlElement savedRoot ("ROOT");
    options->saveParameters (&savedRoot);
    auto* savedPane = savedRoot.getFirstChildElement();
    ASSERT_NE (savedPane, nullptr);
    EXPECT_EQ (savedPane->getStringAttribute ("spikeRaster"), "OFF");
    EXPECT_FALSE (savedPane->getBoolAttribute ("subtractOffset", true));

    savedPane->setAttribute ("spikeRaster", "-100");
    savedPane->setAttribute ("subtractOffset", false);
    options->loadParameters (&savedRoot);
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -100.0f);
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");
    EXPECT_EQ (listener.callbackCount.load(), 0);

    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_FALSE (display->getMedianOffsetPlotting());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_FALSE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "Off");

    savedPane->setAttribute ("spikeRaster", "-150");
    savedPane->setAttribute ("subtractOffset", true);
    listener.callbackCount.store (0);
    options->loadParameters (&savedRoot);
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -150.0f);
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");
    EXPECT_EQ (listener.callbackCount.load(), 0);

    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");

    savedPane->setAttribute ("spikeRaster", "OFF");
    savedPane->setAttribute ("subtractOffset", false);
    listener.callbackCount.store (0);
    options->loadParameters (&savedRoot);
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_FALSE (display->getMedianOffsetPlotting());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_FALSE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "Off");
    EXPECT_EQ (listener.callbackCount.load(), 0);

    savedPane->setAttribute ("spikeRaster", "-200");
    options->loadParameters (&savedRoot);
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    savedPane->removeAttribute ("spikeRaster");
    listener.callbackCount.store (0);
    options->loadParameters (&savedRoot);
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_FALSE (display->getMedianOffsetPlotting());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_FALSE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "Off");
    EXPECT_EQ (listener.callbackCount.load(), 0);

    button->removeListener (&listener);
    processor->stopAcquisition();
}

TEST_F (LfpDisplayNodeTests,
        SpikeRasterPersistsOffsetOwnershipAcrossXmlAndCustomThresholds)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->resized();
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    processor->startAcquisition();
    canvas->beginAnimation();
    auto input = createBuffer (0.0f, 1.0f, numChannels, 128);
    writeBlock (input);
    canvas->refreshState();
    canvas->toggleOptionsDrawer (true);

    const auto id = "oe.processor." + String (processor->getNodeId())
                    + ".lfp.display_1.subtract_offset";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (button, nullptr);
    ASSERT_NE (display, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    std::vector<ComboBox*> comboBoxes;
    collectLfpDescendants (*options, comboBoxes);
    const auto spikeRaster = std::find_if (
        comboBoxes.begin(), comboBoxes.end(), [] (const ComboBox* candidate)
        { return candidate->getName() == "spikeRasterSelection"; });
    ASSERT_NE (spikeRaster, comboBoxes.end());

    options->setMedianOffset (false);
    (*spikeRaster)->setSelectedId (2, sendNotificationSync);
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    XmlElement autoOwnedRoot ("ROOT");
    options->saveParameters (&autoOwnedRoot);
    auto* autoOwnedPane = autoOwnedRoot.getFirstChildElement();
    ASSERT_NE (autoOwnedPane, nullptr);
    EXPECT_FALSE (autoOwnedPane->getBoolAttribute ("subtractOffset", true));
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (display->getMedianOffsetPlotting());
    options->loadParameters (&autoOwnedRoot);
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (display->getMedianOffsetPlotting());

    options->setMedianOffset (true);
    (*spikeRaster)->setSelectedId (2, sendNotificationSync);
    XmlElement userOwnedRoot ("ROOT");
    options->saveParameters (&userOwnedRoot);
    auto* userOwnedPane = userOwnedRoot.getFirstChildElement();
    ASSERT_NE (userOwnedPane, nullptr);
    EXPECT_TRUE (userOwnedPane->getBoolAttribute ("subtractOffset", false));
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    options->loadParameters (&userOwnedRoot);
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_TRUE (display->getMedianOffsetPlotting());

    options->setMedianOffset (false);
    (*spikeRaster)->setSelectedId (2, sendNotificationSync);
    (*spikeRaster)->setText ("-123", sendNotificationSync);
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -123.0f);
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_FALSE (display->getMedianOffsetPlotting());

    options->setMedianOffset (true);
    (*spikeRaster)->setSelectedId (2, sendNotificationSync);
    (*spikeRaster)->setText ("-234", sendNotificationSync);
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -234.0f);
    (*spikeRaster)->setSelectedId (1, sendNotificationSync);
    EXPECT_TRUE (display->getMedianOffsetPlotting());

    processor->stopAcquisition();
}

TEST_F (LfpDisplayNodeTests,
        ShowChannelNumbersChangesTheRealChannelInfoTooltipConsumer)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (display, nullptr);
    ASSERT_GT (display->channelInfo.size(), 3);
    auto* channelInfo = display->channelInfo[3];
    ASSERT_NE (channelInfo, nullptr);
    auto* tooltip = dynamic_cast<TooltipClient*> (channelInfo);
    ASSERT_NE (tooltip, nullptr);

    display->options->setShowChannelNumbers (false);
    EXPECT_EQ (tooltip->getTooltip(), "CH3");
    display->options->setShowChannelNumbers (true);
    EXPECT_EQ (tooltip->getTooltip(), "4");
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesShowChannelNumbers)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    const auto id = prefix + "1.show_channel_numbers";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    ASSERT_NE (button, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    const auto window = static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);
    const auto runAction = [&] (StringRef targetId, LfpWindowsUiaAction action)
    {
        LfpWindowsUiaInvokeResult actionResult;
        std::atomic<bool> workerReturned { false };
        std::thread worker ([&]
        {
            actionResult = invokeLfpWindowsUiaControl (
                window, std::wstring (String (targetId).toWideCharPointer()), action);
            workerReturned.store (true);
        });
        for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
            MessageManager::getInstance()->runDispatchLoopUntil (10);
        joinLfpWorkerOrAbort (worker, workerReturned);
        return actionResult;
    };

    const auto description =
        L"Show channel numbers instead of channel names in LFP display 1. This changes labels and tooltips only; acquisition and recording are unaffected.";
    const auto initialResult = runAction (id, LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (initialResult.invokeResult, S_OK);
    EXPECT_EQ (initialResult.controlType, UIA_CheckBoxControlTypeId);
    EXPECT_EQ (initialResult.enabled, TRUE);
    EXPECT_EQ (initialResult.name, L"LFP display 1 show channel numbers");
    EXPECT_EQ (initialResult.help, description);
    EXPECT_TRUE (initialResult.togglePatternAvailable);
    EXPECT_EQ (initialResult.toggleState, ToggleState_Off);
    EXPECT_TRUE (initialResult.valuePatternAvailable);
    EXPECT_EQ (initialResult.valueReadOnly, TRUE);
    EXPECT_EQ (initialResult.value, L"Off");
    EXPECT_FALSE (initialResult.invokePatternAvailable);

    const auto enabledResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (enabledResult.invokeResult, S_OK);
    EXPECT_EQ (enabledResult.toggleState, ToggleState_On);
    EXPECT_EQ (enabledResult.value, L"On");
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (options->getChannelNameState());

    button->setEnabled (false);
    const auto disabledResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (disabledResult.invokeResult,
               static_cast<HRESULT> (UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (options->getChannelNameState());
    const auto hiddenPaneResult = runAction (
        prefix + "2.show_channel_numbers", LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (hiddenPaneResult.invokeResult, E_FAIL);

    button->setEnabled (true);
    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult = runAction (id, LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (closedDrawerResult.invokeResult, E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        InvertSignalToggleRunsOnMessageThreadAndSurvivesEdgeCases)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id = "oe.processor." + String (processor->getNodeId())
                    + ".lfp.display_1.invert_signal";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    ASSERT_NE (button, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    auto* handler = button->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    const auto actions = handler->getActions();

    LfpThreadTrackingButtonListener listener;
    button->addListener (&listener);
    std::atomic<bool> workerReturned { false };
    bool toggled = false;
    std::thread worker ([&]
    {
        toggled = actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100
                        && (! workerReturned.load()
                            || ! options->getInputInvertedState());
         ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (worker, workerReturned);
    button->removeListener (&listener);
    EXPECT_TRUE (toggled);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
    EXPECT_TRUE (options->getInputInvertedState());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");

    options->setInputInverted (false);
    button->setEnabled (false);
    workerReturned.store (false);
    std::thread disabledWorker ([&]
    {
        toggled = actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (disabledWorker, workerReturned);
    EXPECT_TRUE (toggled);
    EXPECT_FALSE (options->getInputInvertedState());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_FALSE (handler->isEnabled());
    button->setEnabled (true);

    auto zeroChannelTester = std::make_unique<ProcessorTester> (
        TestSourceNodeBuilder (FakeSourceNodeParams { 0, sampleRate, bitVolts }));
    auto* zeroChannelProcessor = zeroChannelTester->createProcessor<LfpViewer::LfpDisplayNode> (
        Plugin::Processor::SINK);
    auto zeroCanvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        zeroChannelProcessor, LfpViewer::SplitLayouts::SINGLE, false);
    zeroCanvas->updateSettings();
    zeroCanvas->setSize (900, 800);
    zeroCanvas->addToDesktop (0);
    zeroCanvas->setVisible (true);
    zeroCanvas->toggleOptionsDrawer (true);
    zeroCanvas->removeBufferForDisplay (0);
    const auto zeroId = "oe.processor." + String (zeroChannelProcessor->getNodeId())
                        + ".lfp.display_1.invert_signal";
    auto* zeroButton = dynamic_cast<Button*> (
        findLfpDescendantById (*zeroCanvas, zeroId));
    ASSERT_NE (zeroButton, nullptr);
    auto* zeroOptions = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*zeroButton);
    ASSERT_NE (zeroOptions, nullptr);
    EXPECT_TRUE (zeroButton->getAccessibilityHandler()->getActions().invoke (
        AccessibilityActionType::toggle));
    EXPECT_TRUE (zeroOptions->getInputInvertedState());
    EXPECT_TRUE (zeroButton->getToggleState());

    canvas.reset();
    workerReturned.store (false);
    std::thread destroyedWorker ([&]
    {
        actions.invoke (AccessibilityActionType::toggle);
        workerReturned.store (true);
    });
    for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (destroyedWorker, workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesSubtractOffsetAndKeepsSpikeRasterOn)
{
    auto input = createBuffer (0.0f, 1.0f, numChannels, 128);
    writeBlock (input);
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->updateSettings();
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    const auto id = prefix + "1.subtract_offset";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (button, nullptr);
    ASSERT_NE (display, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    const auto window = static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);
    const auto runAction = [&] (StringRef targetId, LfpWindowsUiaAction action)
    {
        LfpWindowsUiaInvokeResult actionResult;
        std::atomic<bool> workerReturned { false };
        std::thread worker ([&]
        {
            actionResult = invokeLfpWindowsUiaControl (
                window, std::wstring (String (targetId).toWideCharPointer()), action);
            workerReturned.store (true);
        });
        for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
            MessageManager::getInstance()->runDispatchLoopUntil (10);
        joinLfpWorkerOrAbort (worker, workerReturned);
        return actionResult;
    };

    const auto description =
        L"Subtract each channel's display offset, computed from its current screen-buffer mean, in LFP display 1. Spike raster plotting requires this option and keeps it on. This affects display rendering only; acquisition and recording are unaffected.";
    const auto initialResult = runAction (id, LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (initialResult.invokeResult, S_OK);
    EXPECT_EQ (initialResult.controlType, UIA_CheckBoxControlTypeId);
    EXPECT_EQ (initialResult.enabled, TRUE);
    EXPECT_EQ (initialResult.name, L"LFP display 1 subtract offset");
    EXPECT_EQ (initialResult.help, description);
    EXPECT_TRUE (initialResult.togglePatternAvailable);
    EXPECT_EQ (initialResult.toggleState, ToggleState_Off);
    EXPECT_TRUE (initialResult.valuePatternAvailable);
    EXPECT_EQ (initialResult.valueReadOnly, TRUE);
    EXPECT_EQ (initialResult.value, L"Off");
    EXPECT_FALSE (initialResult.invokePatternAvailable);

    const auto enabledResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (enabledResult.invokeResult, S_OK);
    EXPECT_EQ (enabledResult.toggleState, ToggleState_On);
    EXPECT_EQ (enabledResult.value, L"On");
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (display->getMedianOffsetPlotting());

    button->setEnabled (false);
    const auto disabledResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (disabledResult.invokeResult,
               static_cast<HRESULT> (UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (display->getMedianOffsetPlotting());
    const auto hiddenPaneResult = runAction (
        prefix + "2.subtract_offset", LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (hiddenPaneResult.invokeResult, E_FAIL);

    button->setEnabled (true);
    std::vector<ComboBox*> comboBoxes;
    collectLfpDescendants (*options, comboBoxes);
    const auto spikeRaster = std::find_if (
        comboBoxes.begin(), comboBoxes.end(), [] (const ComboBox* candidate)
        { return candidate->getName() == "spikeRasterSelection"; });
    ASSERT_NE (spikeRaster, comboBoxes.end());
    (*spikeRaster)->setSelectedId (2, sendNotification);
    const auto forcedOnResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (forcedOnResult.invokeResult, S_OK);
    EXPECT_EQ (forcedOnResult.toggleState, ToggleState_On);
    EXPECT_EQ (forcedOnResult.value, L"On");
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (display->getMedianOffsetPlotting());

    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult = runAction (id, LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (closedDrawerResult.invokeResult, E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        InvertSignalXmlRoundTripsPerPaneWithoutNotifications)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    std::array<Button*, 3> buttons {};
    std::array<LfpViewer::LfpDisplayOptions*, 3> options {};
    std::array<AccessibilityValueInterface*, 3> values {};
    std::array<LfpThreadTrackingButtonListener, 3> listeners;
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        buttons[displayIndex] = dynamic_cast<Button*> (findLfpDescendantById (
            *canvas, prefix + String (displayIndex + 1) + ".invert_signal"));
        ASSERT_NE (buttons[displayIndex], nullptr);
        options[displayIndex] = findLfpAncestor<LfpViewer::LfpDisplayOptions> (
            *buttons[displayIndex]);
        ASSERT_NE (options[displayIndex], nullptr);
        values[displayIndex] = buttons[displayIndex]->getAccessibilityHandler()
                                   ->getValueInterface();
        ASSERT_NE (values[displayIndex], nullptr);
        buttons[displayIndex]->addListener (&listeners[displayIndex]);
        options[displayIndex]->setInputInverted (displayIndex != 1);
    }

    XmlElement savedRoot ("ROOT");
    for (auto* paneOptions : options)
        paneOptions->saveParameters (&savedRoot);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        auto* savedPane = savedRoot.getChildByName (
            "LFPDISPLAY" + String (displayIndex));
        ASSERT_NE (savedPane, nullptr);
        EXPECT_EQ (savedPane->getBoolAttribute ("isInverted"),
                   displayIndex != 1);
        options[displayIndex]->setInputInverted (false);
    }
    for (auto* paneOptions : options)
        paneOptions->loadParameters (&savedRoot);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        const auto expected = displayIndex != 1;
        EXPECT_EQ (options[displayIndex]->getInputInvertedState(), expected);
        EXPECT_EQ (buttons[displayIndex]->getToggleState(), expected);
        EXPECT_EQ (buttons[displayIndex]->getAccessibilityHandler()
                       ->getCurrentState().isChecked(), expected);
        EXPECT_EQ (values[displayIndex]->getCurrentValueAsString(),
                   expected ? String ("On") : String ("Off"));
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
    }
    for (auto* savedPane : savedRoot.getChildIterator())
        savedPane->removeAttribute ("isInverted");
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        options[displayIndex]->setInputInverted (true);
        options[displayIndex]->loadParameters (&savedRoot);
        EXPECT_FALSE (options[displayIndex]->getInputInvertedState());
        EXPECT_FALSE (buttons[displayIndex]->getToggleState());
        EXPECT_FALSE (buttons[displayIndex]->getAccessibilityHandler()
                          ->getCurrentState().isChecked());
        EXPECT_EQ (values[displayIndex]->getCurrentValueAsString(), "Off");
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
        buttons[displayIndex]->removeListener (&listeners[displayIndex]);
    }
}

TEST_F (LfpDisplayNodeTests,
        InvertSignalChangesOnlyInvertibleDisplayedChannels)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (display, nullptr);
    ASSERT_GT (display->channels.size(), 2);
    auto* contextMenuChannel = display->channels[0];
    auto* nonInvertibleChannel = display->channels[1];
    auto* anotherInvertibleChannel = display->channels[2];
    ASSERT_NE (contextMenuChannel, nullptr);
    ASSERT_NE (nonInvertibleChannel, nullptr);
    ASSERT_NE (anotherInvertibleChannel, nullptr);

    contextMenuChannel->changeParameter (1);
    EXPECT_TRUE (contextMenuChannel->getInputInverted());
    EXPECT_FALSE (anotherInvertibleChannel->getInputInverted());
    nonInvertibleChannel->setCanBeInverted (false);
    display->options->setInputInverted (true);
    EXPECT_TRUE (display->options->getInputInvertedState());
    EXPECT_TRUE (contextMenuChannel->getInputInverted());
    EXPECT_FALSE (nonInvertibleChannel->getInputInverted());
    EXPECT_TRUE (anotherInvertibleChannel->getInputInverted());

    display->options->setInputInverted (false);
    EXPECT_FALSE (display->options->getInputInvertedState());
    EXPECT_FALSE (contextMenuChannel->getInputInverted());
    EXPECT_FALSE (nonInvertibleChannel->getInputInverted());
    EXPECT_FALSE (anotherInvertibleChannel->getInputInverted());
}

TEST_F (LfpDisplayNodeTests,
        ReversedChannelPopupUsesDrawableTargetForStateAndAction)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::
                    SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (
        1200,
        800);
    canvas->addToDesktop (
        0);
    canvas->setVisible (
        true);

    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        display,
        nullptr);
    ASSERT_GT (
        display->channels.size(),
        1);

    auto* options =
        display->options;
    ASSERT_NE (
        options,
        nullptr);
    options->setChannelsReversed (
        true);
    ASSERT_EQ (
        display->drawableChannels[0].channel,
        display->channels.getLast());
    auto* target =
        display->drawableChannels[0].channel;
    auto* source =
        display->channels[0];
    ASSERT_NE (
        target,
        source);
    for (auto* channel :
         display->channels)
    {
        EXPECT_FALSE (
            LfpChannelSelectionProbe::
                read (
                    *channel));
    }

    target->changeParameter (
        1);
    std::vector<bool> initialInversion;
    for (auto* channel :
         display->channels)
    {
        initialInversion.push_back (
            channel->getInputInverted());
    }

    auto* messageCenter =
        tester->processorGraph
            ->getMessageCenter();
    ASSERT_NE (
        messageCenter,
        nullptr);
    EXPECT_TRUE (
        drainLfpBroadcastMessages (
            *messageCenter)
            .isEmpty());

    struct PopupObservation
    {
        bool foundInvertSignal = false;
        bool invertSignalChecked = false;
        bool pressedInvertSignal = false;
    };
    auto observation =
        std::make_shared<
            PopupObservation>();
    MessageManager::callAsync (
        [observation]
        {
            if (auto* item =
                    findLfpDesktopComponentByAccessibilityTitle (
                        "Invert signal"))
            {
                observation->foundInvertSignal =
                    true;
                if (auto* handler =
                        item->getAccessibilityHandler())
                {
                    observation->invertSignalChecked =
                        handler->getCurrentState()
                            .isChecked();
                    observation->pressedInvertSignal =
                        handler->getActions()
                            .invoke (
                                AccessibilityActionType::
                                    press);
                }
            }

            if (! observation->pressedInvertSignal)
            {
                PopupMenu::
                    dismissAllActiveMenus();
            }
        });

    const auto eventTime =
        Time::getCurrentTime();
    const auto targetPoint =
        Point<float> (
            float (target->getX()
                   + target->getWidth()
                         / 2),
            float (target->getY()
                   + target->getHeight()
                         / 2));
    MouseEvent rightClick (
        Desktop::getInstance()
            .getMainMouseSource(),
        targetPoint,
        ModifierKeys (
            ModifierKeys::
                rightButtonModifier),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        display,
        display,
        eventTime,
        targetPoint,
        eventTime,
        1,
        false);

    display->mouseDown (
        rightClick);

    EXPECT_TRUE (
        observation->foundInvertSignal);
    EXPECT_TRUE (
        observation->invertSignalChecked);
    EXPECT_TRUE (
        observation->pressedInvertSignal);
    EXPECT_TRUE (
        LfpChannelSelectionProbe::read (
            *target));
    EXPECT_FALSE (
        LfpChannelSelectionProbe::read (
            *source));
    EXPECT_FALSE (
        target->getInputInverted());
    for (int index = 0;
         index < display->channels.size();
         ++index)
    {
        if (display->channels[index]
            != target)
        {
            EXPECT_FALSE (
                LfpChannelSelectionProbe::
                    read (
                        *display->channels[
                            index]));
            EXPECT_EQ (
                display->channels[index]
                    ->getInputInverted(),
                initialInversion[
                    static_cast<size_t> (
                        index)]);
        }
    }
    EXPECT_TRUE (
        drainLfpBroadcastMessages (
            *messageCenter)
            .isEmpty());
}

TEST_F (LfpDisplayNodeTests,
        PerChannelInvertDoesNotRequestAudioMonitoring)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    auto* display =
        findLfpDescendant<LfpViewer::LfpDisplay> (
            *canvas);
    ASSERT_NE (display, nullptr);
    ASSERT_FALSE (display->channels.isEmpty());
    auto* channel = display->channels[0];
    ASSERT_NE (channel, nullptr);
    auto* messageCenter =
        tester->processorGraph
            ->getMessageCenter();
    ASSERT_NE (messageCenter, nullptr);
    EXPECT_TRUE (
        drainLfpBroadcastMessages (
            *messageCenter)
            .isEmpty());
    EXPECT_FALSE (
        channel->getInputInverted());

    channel->changeParameter (1);

    EXPECT_TRUE (
        channel->getInputInverted());
    const auto firstMessages =
        drainLfpBroadcastMessages (
            *messageCenter);
    EXPECT_EQ (firstMessages.size(), 0)
        << (firstMessages.isEmpty()
                ? std::string()
                : firstMessages[0]
                      .toStdString());

    channel->changeParameter (1);

    EXPECT_FALSE (
        channel->getInputInverted());
    const auto secondMessages =
        drainLfpBroadcastMessages (
            *messageCenter);
    EXPECT_EQ (secondMessages.size(), 0)
        << (secondMessages.isEmpty()
                ? std::string()
                : secondMessages[0]
                      .toStdString());
}

TEST_F (LfpDisplayNodeTests,
        PerChannelMonitorRequestsAudioExactlyOnce)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    auto* display =
        findLfpDescendant<LfpViewer::LfpDisplay> (
            *canvas);
    ASSERT_NE (display, nullptr);
    ASSERT_FALSE (display->channels.isEmpty());
    auto* channel = display->channels[0];
    ASSERT_NE (channel, nullptr);
    auto* splitter =
        findLfpAncestor<
            LfpViewer::LfpDisplaySplitter> (
            *channel);
    ASSERT_NE (splitter, nullptr);
    auto* messageCenter =
        tester->processorGraph
            ->getMessageCenter();
    ASSERT_NE (messageCenter, nullptr);
    EXPECT_TRUE (
        drainLfpBroadcastMessages (
            *messageCenter)
            .isEmpty());
    const auto expectedMessage =
        "AUDIO SELECT "
        + String (
            int (splitter
                     ->selectedStreamId))
        + " "
        + String (
            display->channels
                .indexOf (
                    channel)
            + 1)
        + " ";

    channel->changeParameter (2);

    EXPECT_FALSE (
        channel->getInputInverted());
    const auto messages =
        drainLfpBroadcastMessages (
            *messageCenter);
    ASSERT_EQ (messages.size(), 1);
    EXPECT_EQ (
        messages[0],
        expectedMessage);
}

TEST_F (LfpDisplayNodeTests,
        UnknownPerChannelMenuCommandsHaveNoSideEffects)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    auto* display =
        findLfpDescendant<LfpViewer::LfpDisplay> (
            *canvas);
    ASSERT_NE (display, nullptr);
    ASSERT_FALSE (display->channels.isEmpty());
    auto* channel = display->channels[0];
    ASSERT_NE (channel, nullptr);
    auto* messageCenter =
        tester->processorGraph
            ->getMessageCenter();
    ASSERT_NE (messageCenter, nullptr);
    EXPECT_TRUE (
        drainLfpBroadcastMessages (
            *messageCenter)
            .isEmpty());

    channel->changeParameter (0);
    channel->changeParameter (37);

    EXPECT_FALSE (
        channel->getInputInverted());
    EXPECT_TRUE (
        drainLfpBroadcastMessages (
            *messageCenter)
            .isEmpty());
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesInvertSignal)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix = "oe.processor." + String (processor->getNodeId())
                        + ".lfp.display_";
    const auto id = prefix + "1.invert_signal";
    auto* button = dynamic_cast<Button*> (findLfpDescendantById (*canvas, id));
    ASSERT_NE (button, nullptr);
    auto* options = findLfpAncestor<LfpViewer::LfpDisplayOptions> (*button);
    ASSERT_NE (options, nullptr);
    const auto window = static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);
    const auto runAction = [&] (StringRef targetId, LfpWindowsUiaAction action)
    {
        LfpWindowsUiaInvokeResult actionResult;
        std::atomic<bool> workerReturned { false };
        std::thread worker ([&]
        {
            actionResult = invokeLfpWindowsUiaControl (
                window, std::wstring (String (targetId).toWideCharPointer()), action);
            workerReturned.store (true);
        });
        for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
            MessageManager::getInstance()->runDispatchLoopUntil (10);
        joinLfpWorkerOrAbort (worker, workerReturned);
        return actionResult;
    };

    const auto description =
        L"Set displayed signal polarity to inverted or normal for all invertible channels in LFP display 1. This changes visualization only; acquisition and recording data are unaffected.";
    const auto initialResult = runAction (id, LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (initialResult.invokeResult, S_OK);
    EXPECT_EQ (initialResult.controlType, UIA_CheckBoxControlTypeId);
    EXPECT_EQ (initialResult.enabled, TRUE);
    EXPECT_EQ (initialResult.name, L"LFP display 1 invert signal polarity");
    EXPECT_EQ (initialResult.help, description);
    EXPECT_TRUE (initialResult.togglePatternAvailable);
    EXPECT_EQ (initialResult.toggleState, ToggleState_Off);
    EXPECT_TRUE (initialResult.valuePatternAvailable);
    EXPECT_EQ (initialResult.valueReadOnly, TRUE);
    EXPECT_EQ (initialResult.value, L"Off");
    EXPECT_FALSE (initialResult.invokePatternAvailable);

    const auto enabledResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (enabledResult.invokeResult, S_OK);
    EXPECT_EQ (enabledResult.toggleState, ToggleState_On);
    EXPECT_EQ (enabledResult.value, L"On");
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (options->getInputInvertedState());

    button->setEnabled (false);
    const auto disabledResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (disabledResult.invokeResult,
               static_cast<HRESULT> (UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (options->getInputInvertedState());
    const auto hiddenPaneResult = runAction (
        prefix + "2.invert_signal", LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (hiddenPaneResult.invokeResult, E_FAIL);

    button->setEnabled (true);
    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult = runAction (id, LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (closedDrawerResult.invokeResult, E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        ReverseOrderToggleRunsOnMessageThreadAndTracksProgrammaticState)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (
        true);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.reverse_order";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* display =
        findLfpDescendant<
            LfpViewer::LfpDisplay> (
            *canvas);
    ASSERT_NE (button, nullptr);
    ASSERT_NE (display, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::LfpDisplayOptions> (
            *button);
    ASSERT_NE (options, nullptr);
    auto* handler =
        button->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    auto* value = handler->getValueInterface();
    ASSERT_NE (value, nullptr);
    const auto actions = handler->getActions();

    LfpThreadTrackingButtonListener listener;
    button->addListener (&listener);
    std::atomic<bool> workerReturned { false };
    bool toggled = false;
    std::thread worker (
        [&]
        {
            toggled = actions.invoke (
                AccessibilityActionType::toggle);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100 && (! workerReturned.load()
                           || ! display->getChannelsReversed());
         ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (worker, workerReturned);
    button->removeListener (&listener);

    EXPECT_TRUE (toggled);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
    EXPECT_TRUE (display->getChannelsReversed());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");

    options->setChannelsReversed (false);
    EXPECT_FALSE (display->getChannelsReversed());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_FALSE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "Off");
    button->setEnabled (false);
    workerReturned.store (false);
    std::thread disabledWorker (
        [&]
        {
            toggled = actions.invoke (
                AccessibilityActionType::toggle);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100 && ! workerReturned.load();
         ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (disabledWorker, workerReturned);
    EXPECT_TRUE (toggled);
    EXPECT_FALSE (display->getChannelsReversed());
    EXPECT_FALSE (button->getToggleState());
    EXPECT_FALSE (handler->isEnabled());

    button->setEnabled (true);
    auto* splitter =
        findLfpDescendant<
            LfpViewer::LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (splitter, nullptr);
    auto* displayBuffer = splitter->displayBuffer;
    splitter->displayBuffer = nullptr;
    LfpThreadTrackingButtonListener nullBufferListener;
    button->addListener (&nullBufferListener);
    workerReturned.store (false);
    toggled = false;
    std::thread nullBufferWorker (
        [&]
        {
            toggled = actions.invoke (
                AccessibilityActionType::toggle);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100 && (! workerReturned.load()
                           || ! display->getChannelsReversed());
         ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (nullBufferWorker, workerReturned);
    button->removeListener (&nullBufferListener);
    EXPECT_TRUE (toggled);
    EXPECT_EQ (nullBufferListener.callbackCount.load(), 1);
    EXPECT_TRUE (nullBufferListener.callbackUsedMessageThread.load());
    EXPECT_TRUE (display->getChannelsReversed());
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (handler->getCurrentState().isChecked());
    EXPECT_EQ (value->getCurrentValueAsString(), "On");
    options->setChannelsReversed (false);
    splitter->displayBuffer = displayBuffer;

    canvas.reset();
    workerReturned.store (false);
    std::thread destroyedWorker (
        [&]
        {
            actions.invoke (AccessibilityActionType::toggle);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100 && ! workerReturned.load();
         ++attempt)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    joinLfpWorkerOrAbort (destroyedWorker, workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        ReverseOrderXmlRoundTripsPerPaneWithoutNotifications)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix = "oe.processor."
                        + String (processor->getNodeId())
                        + ".lfp.display_";
    std::array<Button*, 3> buttons {};
    std::array<LfpViewer::LfpDisplayOptions*, 3> options {};
    std::array<LfpViewer::LfpDisplay*, 3> displays {};
    std::array<AccessibilityValueInterface*, 3> values {};
    std::array<LfpThreadTrackingButtonListener, 3> listeners;
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        buttons[displayIndex] = dynamic_cast<Button*> (
            findLfpDescendantById (*canvas, prefix + String (displayIndex + 1)
                                   + ".reverse_order"));
        ASSERT_NE (buttons[displayIndex], nullptr);
        options[displayIndex] = findLfpAncestor<LfpViewer::LfpDisplayOptions> (
            *buttons[displayIndex]);
        ASSERT_NE (options[displayIndex], nullptr);
        values[displayIndex] = buttons[displayIndex]
                                 ->getAccessibilityHandler()
                                 ->getValueInterface();
        ASSERT_NE (values[displayIndex], nullptr);
        buttons[displayIndex]->addListener (&listeners[displayIndex]);
        options[displayIndex]->setChannelsReversed (displayIndex != 1);
    }
    std::vector<LfpViewer::LfpDisplay*> allDisplays;
    collectLfpDescendants (*canvas, allDisplays);
    ASSERT_EQ (allDisplays.size(), 3);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
        for (auto* candidate : allDisplays)
            if (candidate->options == options[displayIndex])
                displays[displayIndex] = candidate;
    for (auto* display : displays)
        ASSERT_NE (display, nullptr);

    XmlElement savedRoot ("ROOT");
    for (auto* paneOptions : options)
        paneOptions->saveParameters (&savedRoot);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        auto* savedPane = savedRoot.getChildByName (
            "LFPDISPLAY" + String (displayIndex));
        ASSERT_NE (savedPane, nullptr);
        EXPECT_EQ (savedPane->getBoolAttribute ("reverseOrder"),
                   displayIndex != 1);
        options[displayIndex]->setChannelsReversed (false);
    }
    for (auto* paneOptions : options)
        paneOptions->loadParameters (&savedRoot);
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        const auto expected = displayIndex != 1;
        EXPECT_EQ (buttons[displayIndex]->getToggleState(), expected);
        EXPECT_EQ (buttons[displayIndex]->getAccessibilityHandler()
                       ->getCurrentState().isChecked(), expected);
        EXPECT_EQ (values[displayIndex]->getCurrentValueAsString(),
                   expected ? String ("On") : String ("Off"));
        EXPECT_EQ (displays[displayIndex]->getChannelsReversed(), expected);
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
    }
    for (auto* savedPane : savedRoot.getChildIterator())
        savedPane->removeAttribute ("reverseOrder");
    for (int displayIndex = 0; displayIndex < 3; ++displayIndex)
    {
        options[displayIndex]->setChannelsReversed (true);
        options[displayIndex]->loadParameters (&savedRoot);
        EXPECT_FALSE (buttons[displayIndex]->getToggleState());
        EXPECT_FALSE (buttons[displayIndex]->getAccessibilityHandler()
                          ->getCurrentState().isChecked());
        EXPECT_EQ (values[displayIndex]->getCurrentValueAsString(), "Off");
        EXPECT_FALSE (displays[displayIndex]->getChannelsReversed());
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
        buttons[displayIndex]->removeListener (&listeners[displayIndex]);
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesSortByDepthToggleForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (
        true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".sort_by_depth";
        auto* button =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            button,
            nullptr)
            << id;
        auto* handler =
            button
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        const auto description =
            "Turn metadata-based channel ordering on or off in LFP display "
            + String (displayNumber)
            + ". When enabled, visible channels are ordered using available group, depth, and horizontal-position metadata. This changes display order only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                toggleButton);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " sort channels by depth");
        EXPECT_EQ (
            handler
                ->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_TRUE (
            handler
                ->getCurrentState()
                .isCheckable());
        EXPECT_FALSE (
            handler
                ->getCurrentState()
                .isChecked());
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_TRUE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Off");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *button)
                ->isVisible(),
            displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        SortByDepthToggleRunsOnMessageThreadAndTracksProgrammaticState)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (
        true);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.sort_by_depth";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        button,
        nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *button);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        options,
        nullptr);
    ASSERT_NE (
        display,
        nullptr);
    auto* handler =
        button
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    auto* value =
        handler
            ->getValueInterface();
    ASSERT_NE (
        value,
        nullptr);
    const auto actions =
        handler->getActions();

    LfpThreadTrackingButtonListener
        listener;
    button->addListener (
        &listener);
    std::atomic<bool>
        workerReturned { false };
    bool toggled = false;
    std::thread worker (
        [&]
        {
            toggled =
                actions.invoke (
                    AccessibilityActionType::
                        toggle);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || ! display
                           ->shouldOrderChannelsByDepth());
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
    button->removeListener (
        &listener);

    EXPECT_TRUE (toggled);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_TRUE (
        display
            ->shouldOrderChannelsByDepth());
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "On");

    options->setSortByDepth (
        false);
    EXPECT_FALSE (
        display
            ->shouldOrderChannelsByDepth());
    EXPECT_FALSE (
        button->getToggleState());
    EXPECT_FALSE (
        handler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "Off");
    button->setEnabled (
        false);
    workerReturned.store (
        false);
    std::thread disabledWorker (
        [&]
        {
            toggled =
                actions.invoke (
                    AccessibilityActionType::
                        toggle);
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
    joinLfpWorkerOrAbort (
        disabledWorker,
        workerReturned);
    EXPECT_TRUE (toggled);
    EXPECT_FALSE (
        display
            ->shouldOrderChannelsByDepth());
    EXPECT_FALSE (
        button->getToggleState());
    EXPECT_FALSE (
        handler->isEnabled());

    button->setEnabled (
        true);
    auto* splitter =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (
        splitter,
        nullptr);
    auto* displayBuffer =
        splitter->displayBuffer;
    splitter->displayBuffer =
        nullptr;
    options->setSortByDepth (
        true);
    EXPECT_TRUE (
        display
            ->shouldOrderChannelsByDepth());
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        handler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "On");
    options->setSortByDepth (
        false);
    splitter->displayBuffer =
        displayBuffer;

    canvas.reset();
    workerReturned.store (
        false);
    std::thread destroyedWorker (
        [&]
        {
            actions.invoke (
                AccessibilityActionType::
                    toggle);
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
    joinLfpWorkerOrAbort (
        destroyedWorker,
        workerReturned);
}

TEST_F (LfpDisplayNodeTests,
        SortByDepthXmlRoundTripsPerPaneWithoutNotifications)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (
        true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3>
        buttons {};
    std::array<
        LfpViewer::
            LfpDisplayOptions*,
        3>
        options {};
    std::array<
        LfpViewer::
            LfpDisplay*,
        3>
        displays {};
    std::array<
        AccessibilityValueInterface*,
        3>
        values {};
    std::array<
        LfpThreadTrackingButtonListener,
        3>
        listeners;
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        buttons[displayIndex] =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            displayIndex
                            + 1)
                        + ".sort_by_depth"));
        ASSERT_NE (
            buttons[displayIndex],
            nullptr);
        options[displayIndex] =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[
                    displayIndex]);
        ASSERT_NE (
            options[displayIndex],
            nullptr);
        values[displayIndex] =
            buttons[displayIndex]
                ->getAccessibilityHandler()
                ->getValueInterface();
        ASSERT_NE (
            values[displayIndex],
            nullptr);
        buttons[displayIndex]
            ->addListener (
                &listeners[
                    displayIndex]);
        options[displayIndex]
            ->setSortByDepth (
                displayIndex != 1);
    }
    std::vector<
        LfpViewer::
            LfpDisplay*>
        allDisplays;
    collectLfpDescendants (
        *canvas,
        allDisplays);
    ASSERT_EQ (
        allDisplays.size(),
        3);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        for (auto* candidate :
             allDisplays)
        {
            if (candidate->options
                == options[
                    displayIndex])
            {
                displays[displayIndex] =
                    candidate;
                break;
            }
        }
        ASSERT_NE (
            displays[displayIndex],
            nullptr);
    }

    XmlElement savedRoot (
        "ROOT");
    for (auto* paneOptions :
         options)
    {
        paneOptions
            ->saveParameters (
                &savedRoot);
    }
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        auto* savedPane =
            savedRoot
                .getChildByName (
                    "LFPDISPLAY"
                    + String (
                        displayIndex));
        ASSERT_NE (
            savedPane,
            nullptr);
        EXPECT_EQ (
            savedPane
                ->getBoolAttribute (
                    "sortByDepth"),
            displayIndex != 1);
        options[displayIndex]
            ->setSortByDepth (
                false);
    }

    for (auto* paneOptions :
         options)
    {
        paneOptions
            ->loadParameters (
                &savedRoot);
    }
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto expected =
            displayIndex != 1;
        EXPECT_EQ (
            buttons[displayIndex]
                ->getToggleState(),
            expected);
        EXPECT_EQ (
            buttons[displayIndex]
                ->getAccessibilityHandler()
                ->getCurrentState()
                .isChecked(),
            expected);
        EXPECT_EQ (
            values[displayIndex]
                ->getCurrentValueAsString(),
            expected
                ? String ("On")
                : String ("Off"));
        EXPECT_EQ (
            displays[displayIndex]
                ->shouldOrderChannelsByDepth(),
            expected);
        EXPECT_EQ (
            listeners[displayIndex]
                .callbackCount
                .load(),
            0);
    }

    for (auto* savedPane :
         savedRoot
             .getChildIterator())
    {
        savedPane->removeAttribute (
            "sortByDepth");
    }
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        options[displayIndex]
            ->setSortByDepth (
                true);
        options[displayIndex]
            ->loadParameters (
                &savedRoot);
        EXPECT_FALSE (
            buttons[displayIndex]
                ->getToggleState());
        EXPECT_FALSE (
            buttons[displayIndex]
                ->getAccessibilityHandler()
                ->getCurrentState()
                .isChecked());
        EXPECT_EQ (
            values[displayIndex]
                ->getCurrentValueAsString(),
            "Off");
        EXPECT_FALSE (
            displays[displayIndex]
                ->shouldOrderChannelsByDepth());
        EXPECT_EQ (
            listeners[displayIndex]
                .callbackCount
                .load(),
            0);
        buttons[displayIndex]
            ->removeListener (
                &listeners[
                    displayIndex]);
    }
}

TEST_F (LfpDisplayNodeTests,
        SortByDepthOrdersVisibleChannelsByMetadataBeforeReverse)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        display,
        nullptr);
    ASSERT_NE (
        display->options,
        nullptr);
    auto* splitter =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (
        splitter,
        nullptr);
    ASSERT_NE (
        splitter->displayBuffer,
        nullptr);
    ASSERT_EQ (
        display
            ->channelInfo
            .size(),
        16);
    ASSERT_EQ (
        splitter
            ->displayBuffer
            ->channelMetadata
            .size(),
        16);

    const auto getOrder =
        [&]
    {
        std::vector<int> order;
        for (const auto& track :
             display->drawableChannels)
        {
            for (int channel = 0;
                 channel < display
                               ->channelInfo
                               .size();
                 ++channel)
            {
                if (display
                        ->channelInfo[
                            channel]
                    == track.channelInfo)
                {
                    order.push_back (
                        channel);
                    break;
                }
            }
        }
        return order;
    };
    std::vector<int> originalOrder;
    for (int channel = 0;
         channel < 16;
         ++channel)
    {
        originalOrder.push_back (
            channel);
        auto& metadata =
            splitter
                ->displayBuffer
                ->channelMetadata
                .getReference (
                    channel);
        metadata.group = 2;
        metadata.ypos =
            float (channel);
        metadata.xpos = 0.0f;
        metadata.hasGroupMetadata = true;
        metadata.hasYposMetadata = true;
        metadata.hasXposMetadata = true;
    }
    auto& channel0 =
        splitter
            ->displayBuffer
            ->channelMetadata
            .getReference (0);
    channel0.group = 0;
    channel0.ypos = 200.0f;
    splitter
        ->displayBuffer
        ->channelMetadata
        .getReference (2)
        .ypos = 1.0f;
    auto& channel4 =
        splitter
            ->displayBuffer
            ->channelMetadata
            .getReference (4);
    channel4.group = 1;
    channel4.ypos = 100.0f;
    auto& channel8 =
        splitter
            ->displayBuffer
            ->channelMetadata
            .getReference (8);
    channel8.group = 0;
    channel8.ypos = 100.0f;
    channel8.xpos = 10.0f;
    auto& channel12 =
        splitter
            ->displayBuffer
            ->channelMetadata
            .getReference (12);
    channel12.group = 0;
    channel12.ypos = 100.0f;
    channel12.xpos = -5.0f;
    canvas->updateSettings();

    display->options
        ->setSortByDepth (
            false);
    EXPECT_EQ (
        getOrder(),
        originalOrder);

    display->options
        ->setSortByDepth (
            true);
    const std::vector<int>
        sortedOrder {
            12,
            8,
            0,
            4,
            1,
            2,
            3,
            5,
            6,
            7,
            9,
            10,
            11,
            13,
            14,
            15
        };
    EXPECT_EQ (
        getOrder(),
        sortedOrder);

    display->options
        ->setChannelsReversed (
            true);
    auto reversedOrder =
        sortedOrder;
    std::reverse (
        reversedOrder.begin(),
        reversedOrder.end());
    EXPECT_EQ (
        getOrder(),
        reversedOrder);

    display->options
        ->setChannelsReversed (
            false);
    display->options
        ->setSortByDepth (
            false);
    for (int channel = 0;
         channel < 16;
         ++channel)
    {
        auto& metadata =
            splitter
                ->displayBuffer
                ->channelMetadata
                .getReference (
                    channel);
        metadata.group = 0;
        metadata.ypos = 0.0f;
        metadata.xpos = 0.0f;
        metadata.hasGroupMetadata = false;
        metadata.hasYposMetadata = false;
        metadata.hasXposMetadata = false;
    }
    canvas->updateSettings();
    display->options
        ->setSortByDepth (
            true);
    EXPECT_EQ (
        getOrder(),
        originalOrder);
}

TEST_F (LfpDisplayNodeTests,
        ExposesRangeTypeSelectorsForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const std::array<String, 3>
        typeNames {
            "DATA",
            "AUX",
            "ADC"
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        for (int typeIndex = 0;
             typeIndex < 3;
             ++typeIndex)
        {
            const auto typeName =
                typeNames[typeIndex];
            const auto id =
                prefix
                + String (displayNumber)
                + ".channel_type."
                + typeName
                      .toLowerCase();
            auto* component =
                findLfpDescendantById (
                    *canvas,
                    id);
            ASSERT_NE (
                component,
                nullptr)
                << id;
            auto* handler =
                component
                    ->getAccessibilityHandler();
            ASSERT_NE (
                handler,
                nullptr);
            EXPECT_EQ (
                handler->getRole(),
                AccessibilityRole::
                    radioButton);
            EXPECT_EQ (
                handler->getTitle(),
                "LFP display "
                    + String (
                        displayNumber)
                    + " "
                    + "channel type "
                    + typeName);
            const auto description =
                "Select "
                + typeName
                + " channels as the target for the voltage range control in LFP display "
                + String (
                    displayNumber)
                + ".";
            EXPECT_EQ (
                handler
                    ->getDescription(),
                description);
            EXPECT_EQ (
                handler->getHelp(),
                description);
            EXPECT_EQ (
                handler
                    ->getCurrentState()
                    .isChecked(),
                typeIndex == 0);
            EXPECT_TRUE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            press));
            EXPECT_FALSE (
                handler->getActions()
                    .contains (
                        AccessibilityActionType::
                            toggle));
            EXPECT_FALSE (
                handler
                    ->getCurrentState()
                    .isCheckable());
            EXPECT_EQ (
                handler
                    ->getValueInterface(),
                nullptr);
            EXPECT_EQ (
                findLfpAncestor<
                    LfpViewer::
                        LfpDisplayOptions> (
                    *component)
                    ->isVisible(),
                displayIndex == 0);
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesColourGroupingForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const std::array<String, 6>
        choices {
            "1",
            "2",
            "4",
            "8",
            "16",
            "By Shank"
        };
    const std::array<String, 6>
        choiceIds {
            "1",
            "2",
            "4",
            "8",
            "16",
            "by_shank"
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".colour_grouping";
        auto* grouping =
            dynamic_cast<
                MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            grouping,
            nullptr)
            << id;
        EXPECT_EQ (
            grouping->getNumItems(),
            static_cast<int> (
                choices.size()));
        for (int choiceIndex = 0;
             choiceIndex
             < static_cast<int> (
                   choices.size());
             ++choiceIndex)
        {
            EXPECT_EQ (
                grouping->getItemText (
                    choiceIndex),
                choices[choiceIndex]);
        }

        int choiceIndex = 0;
        for (PopupMenu::
                 MenuItemIterator
                 iterator (
                     *grouping
                          ->getRootMenu(),
                     false);
             iterator.next();)
        {
            ASSERT_LT (
                choiceIndex,
                static_cast<int> (
                    choices.size()));
            EXPECT_EQ (
                iterator
                    .getItem()
                    .accessibilityId,
                id
                    + ".choice."
                    + choiceIds[choiceIndex]);
            ++choiceIndex;
        }
        EXPECT_EQ (
            choiceIndex,
            static_cast<int> (
                choices.size()));

        auto* handler =
            grouping
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (
                    displayNumber)
                + " colour grouping");
        const auto description =
            "Choose how LFP display "
            + String (
                displayNumber)
            + " assigns display colours: 1, 2, 4, 8, or 16 groups that many adjacent channels, while By Shank uses channel group metadata with a legacy depth-based fallback. This changes display colours only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler
                ->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_FALSE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "1");
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        expand));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        collapse));
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *grouping)
                ->isVisible(),
            displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesChannelSkipForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const std::array<String, 7>
        choices {
            "None",
            "2",
            "4",
            "8",
            "16",
            "32",
            "64"
        };
    const std::array<String, 7>
        choiceIds {
            "none",
            "2",
            "4",
            "8",
            "16",
            "32",
            "64"
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".channel_skip";
        auto* channelSkip =
            dynamic_cast<
                MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            channelSkip,
            nullptr)
            << id;
        EXPECT_EQ (
            channelSkip->getNumItems(),
            static_cast<int> (
                choices.size()));
        for (int choiceIndex = 0;
             choiceIndex
             < static_cast<int> (
                   choices.size());
             ++choiceIndex)
        {
            EXPECT_EQ (
                channelSkip->getItemText (
                    choiceIndex),
                choices[choiceIndex]);
        }

        int choiceIndex = 0;
        for (PopupMenu::MenuItemIterator
                 iterator (
                     *channelSkip
                          ->getRootMenu(),
                     false);
             iterator.next();)
        {
            ASSERT_LT (
                choiceIndex,
                static_cast<int> (
                    choices.size()));
            EXPECT_EQ (
                iterator
                    .getItem()
                    .accessibilityId,
                id
                    + ".choice."
                    + choiceIds[choiceIndex]);
            ++choiceIndex;
        }
        EXPECT_EQ (
            choiceIndex,
            static_cast<int> (
                choices.size()));

        auto* handler =
            channelSkip
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (displayNumber)
                + " channel skip");
        const auto description =
            "Choose the visible-channel stride for LFP display "
            + String (displayNumber)
            + ": None shows every eligible channel; 2, 4, 8, 16, 32, or 64 shows every Nth eligible channel. This changes display density only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        auto* value =
            handler->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_FALSE (
            value->isReadOnly());
        EXPECT_EQ (
            value->getCurrentValueAsString(),
            "None");
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *channelSkip)
                ->isVisible(),
            displayIndex == 0);
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesSpikeRasterForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const std::array<String, 8>
        choices {
            "OFF",
            "-50",
            "-100",
            "-150",
            "-200",
            "-300",
            "-400",
            "-500"
        };
    const std::array<String, 8>
        choiceIds {
            "off",
            "minus_50",
            "minus_100",
            "minus_150",
            "minus_200",
            "minus_300",
            "minus_400",
            "minus_500"
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".spike_raster";
        auto* spikeRaster =
            dynamic_cast<
                MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            spikeRaster,
            nullptr)
            << id;
        EXPECT_EQ (
            spikeRaster->getName(),
            "spikeRasterSelection");
        EXPECT_EQ (
            spikeRaster->getNumItems(),
            static_cast<int> (
                choices.size()));
        for (int choiceIndex = 0;
             choiceIndex
             < static_cast<int> (
                   choices.size());
             ++choiceIndex)
        {
            EXPECT_EQ (
                spikeRaster->getItemText (
                    choiceIndex),
                choices[choiceIndex]);
        }

        int choiceIndex = 0;
        for (PopupMenu::MenuItemIterator
                 iterator (
                     *spikeRaster
                          ->getRootMenu(),
                     false);
             iterator.next();)
        {
            ASSERT_LT (
                choiceIndex,
                static_cast<int> (
                    choiceIds.size()));
            EXPECT_EQ (
                iterator
                    .getItem()
                    .accessibilityId,
                id
                    + ".choice."
                    + choiceIds[choiceIndex]);
            ++choiceIndex;
        }
        EXPECT_EQ (
            choiceIndex,
            static_cast<int> (
                choiceIds.size()));

        auto* handler =
            spikeRaster
                ->getAccessibilityHandler();
        ASSERT_NE (
            handler,
            nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (displayNumber)
                + " spike raster threshold");
        const auto description =
            "Choose the negative-going spike raster threshold for LFP display "
            + String (displayNumber)
            + ": OFF shows continuous traces; -50, -100, -150, -200, -300, -400, -500, or a custom non-zero magnitude up to 500 replaces continuous traces with raster marks where the signal crosses the threshold relative to its per-pixel mean. Spike raster keeps subtract offset on. This changes display rendering only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        auto* value =
            handler->getValueInterface();
        ASSERT_NE (
            value,
            nullptr);
        EXPECT_FALSE (
            value->isReadOnly());
        EXPECT_EQ (
            value->getCurrentValueAsString(),
            "OFF");
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::press));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::showMenu));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::expand));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::collapse));
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *spikeRaster)
                ->isVisible(),
            displayIndex == 0);
    }

    std::vector<Label*> labels;
    collectLfpDescendants (
        *canvas,
        labels);
    for (const auto* label : labels)
    {
        if (label->getName()
            == "SpikeRasterLabel")
        {
            EXPECT_FALSE (
                label->isAccessible());
        }
    }
    EXPECT_EQ (
        std::count_if (
            labels.begin(),
            labels.end(),
            [] (const Label* label)
            {
                return label->getName()
                       == "SpikeRasterLabel";
            }),
        3);
}

TEST_F (LfpDisplayNodeTests,
        ExposesTriggerSourceForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".trigger_source";
        auto* triggerSource =
            dynamic_cast<
                MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            triggerSource,
            nullptr)
            << id;
        EXPECT_EQ (
            triggerSource->getName(),
            "Trigger Source");
        EXPECT_EQ (
            triggerSource->getNumItems(),
            17);
        for (int choiceIndex = 0;
             choiceIndex < 17;
             ++choiceIndex)
        {
            EXPECT_EQ (
                triggerSource->getItemText (
                    choiceIndex),
                choiceIndex == 0
                    ? String ("None")
                    : String (choiceIndex));
        }

        int choiceIndex = 0;
        for (PopupMenu::MenuItemIterator
                 iterator (
                     *triggerSource
                          ->getRootMenu(),
                     false);
             iterator.next();)
        {
            const auto suffix =
                choiceIndex == 0
                    ? String ("none")
                    : "line_"
                          + String (
                              choiceIndex);
            EXPECT_EQ (
                iterator
                    .getItem()
                    .accessibilityId,
                id
                    + ".choice."
                    + suffix);
            ++choiceIndex;
        }
        EXPECT_EQ (choiceIndex, 17);

        auto* handler =
            triggerSource
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (displayNumber)
                + " trigger source");
        const auto description =
            "Choose the TTL line that triggers LFP display "
            + String (displayNumber)
            + ": None uses continuous scrolling; 1 through 16 starts each triggered view from the corresponding TTL event line. This changes display timing only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        auto* value =
            handler->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_FALSE (value->isReadOnly());
        EXPECT_EQ (
            value->getCurrentValueAsString(),
            "None");
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::press));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::showMenu));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::expand));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::collapse));
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *triggerSource)
                ->isVisible(),
            displayIndex == 0);
    }

    std::vector<Label*> labels;
    collectLfpDescendants (
        *canvas,
        labels);
    for (const auto* label : labels)
    {
        if (label->getName()
            == "TriggerSourceLabel")
        {
            EXPECT_FALSE (
                label->isAccessible());
        }
    }
    EXPECT_EQ (
        std::count_if (
            labels.begin(),
            labels.end(),
            [] (const Label* label)
            {
                return label->getName()
                       == "TriggerSourceLabel";
            }),
        3);
}

TEST_F (LfpDisplayNodeTests,
        TriggerSourceWorkerValuesTrackExactModelAndSurviveTeardown)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.trigger_source";
    auto* triggerSource =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* split =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (triggerSource, nullptr);
    ASSERT_NE (split, nullptr);
    auto liveHandler =
        triggerSource
            ->createAccessibilityHandler();
    ASSERT_NE (liveHandler, nullptr);
    auto* value =
        liveHandler
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    const auto writeFromWorker =
        [&] (StringRef newValue)
    {
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                value->setValueAsString (
                    String (newValue));
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
    };

    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    LfpThreadTrackingComboBoxListener
        listener;
    triggerSource->addListener (&listener);
    writeFromWorker ("8");
    triggerSource->removeListener (&listener);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_EQ (
        triggerSource->getSelectedId(),
        9);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "8");
    EXPECT_EQ (
        split->getTriggerChannel(),
        7);
    EXPECT_TRUE (
        split->isInTriggeredMode());

    const std::array<
        std::tuple<
            String,
            int,
            int,
            bool>,
        4>
        cases {
            std::tuple<String, int, int, bool> {
                "None", 1, -1, false },
            std::tuple<String, int, int, bool> {
                "1", 2, 0, true },
            std::tuple<String, int, int, bool> {
                "8", 9, 7, true },
            std::tuple<String, int, int, bool> {
                "16", 17, 15, true }
        };
    for (const auto& [
             text,
             selectedId,
             triggerLine,
             triggered] :
         cases)
    {
        writeFromWorker (text);
        EXPECT_EQ (
            triggerSource->getSelectedId(),
            selectedId);
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            text);
        EXPECT_EQ (
            split->getTriggerChannel(),
            triggerLine);
        EXPECT_EQ (
            split->isInTriggeredMode(),
            triggered);
    }

    triggerSource->setEnabled (false);
    writeFromWorker ("None");
    EXPECT_EQ (
        triggerSource->getSelectedId(),
        17);
    EXPECT_EQ (
        split->getTriggerChannel(),
        15);

    triggerSource->setEnabled (true);
    triggerSource
        ->synchroniseAccessibilityState();
    auto retainedHandler =
        triggerSource
            ->createAccessibilityHandler();
    ASSERT_NE (retainedHandler, nullptr);
    auto* retainedValue =
        retainedHandler
            ->getValueInterface();
    ASSERT_NE (retainedValue, nullptr);
    const auto retainedActions =
        retainedHandler
            ->getActions();

    canvas.reset();
    std::atomic<bool>
        workerReturned { false };
    bool stalePressFound = false;
    bool staleShowMenuFound = false;
    std::thread staleWorker (
        [&]
        {
            retainedValue
                ->setValueAsString (
                    "None");
            stalePressFound =
                retainedActions.invoke (
                    AccessibilityActionType::
                        press);
            staleShowMenuFound =
                retainedActions.invoke (
                    AccessibilityActionType::
                        showMenu);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
    EXPECT_EQ (
        retainedValue
            ->getCurrentValueAsString(),
        "16");
    EXPECT_TRUE (stalePressFound);
    EXPECT_TRUE (staleShowMenuFound);
}

TEST_F (LfpDisplayNodeTests,
        TriggerSourceValueSurvivesZeroChannelsBufferRemovalAndXmlRestore)
{
    auto zeroChannelTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (
            zeroChannelProcessor
                ->getNodeId())
        + ".lfp.display_1.trigger_source";
    auto* triggerSource =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* split =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (triggerSource, nullptr);
    ASSERT_NE (split, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *triggerSource);
    ASSERT_NE (options, nullptr);
    auto* value =
        triggerSource
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    value->setValueAsString ("16");
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "16");
    EXPECT_EQ (
        split->getTriggerChannel(),
        15);
    canvas->removeBufferForDisplay (0);
    value->setValueAsString ("None");
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "None");
    EXPECT_EQ (
        split->getTriggerChannel(),
        -1);
    EXPECT_FALSE (
        split->isInTriggeredMode());

    value->setValueAsString ("8");
    XmlElement savedRoot ("ROOT");
    options->saveParameters (&savedRoot);
    auto* savedPane =
        savedRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (savedPane, nullptr);
    EXPECT_EQ (
        savedPane->getIntAttribute (
            "triggerSource"),
        9);

    value->setValueAsString ("None");
    LfpThreadTrackingComboBoxListener
        listener;
    triggerSource->addListener (&listener);
    options->loadParameters (&savedRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "8");
    EXPECT_EQ (
        split->getTriggerChannel(),
        7);
    EXPECT_TRUE (
        split->isInTriggeredMode());

    XmlElement noneRoot (savedRoot);
    auto* nonePane =
        noneRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (nonePane, nullptr);
    nonePane->setAttribute (
        "triggerSource",
        1);
    options->loadParameters (&noneRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "None");
    EXPECT_EQ (
        split->getTriggerChannel(),
        -1);

    XmlElement missingRoot (savedRoot);
    auto* missingPane =
        missingRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (missingPane, nullptr);
    missingPane->removeAttribute (
        "triggerSource");
    options->loadParameters (
        &missingRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "None");
    EXPECT_EQ (
        split->getTriggerChannel(),
        -1);

    XmlElement malformedRoot (
        savedRoot);
    auto* malformedPane =
        malformedRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (malformedPane, nullptr);
    malformedPane->setAttribute (
        "triggerSource",
        999);
    options->loadParameters (
        &malformedRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "None");
    EXPECT_EQ (
        split->getTriggerChannel(),
        -1);

    XmlElement legacyZeroRoot (
        savedRoot);
    auto* legacyZeroPane =
        legacyZeroRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (legacyZeroPane, nullptr);
    legacyZeroPane->setAttribute (
        "triggerSource",
        0);
    options->loadParameters (
        &legacyZeroRoot);
    triggerSource->removeListener (
        &listener);
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);
    EXPECT_EQ (
        triggerSource->getSelectedId(),
        1);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "None");
    EXPECT_EQ (
        split->getTriggerChannel(),
        -1);
    EXPECT_FALSE (
        split->isInTriggeredMode());
}

TEST_F (LfpDisplayNodeTests,
        ExposesColourSchemeForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const std::array<String, 8>
        choices {
            "Classic",
            "Monochrome Gray",
            "Monochrome Yellow",
            "Monochrome Purple",
            "Monochrome Green",
            "Open Ephys Logo",
            "Tropical",
            "Light Background"
        };
    const std::array<String, 8>
        choiceIds {
            "classic",
            "monochrome_gray",
            "monochrome_yellow",
            "monochrome_purple",
            "monochrome_green",
            "open_ephys_logo",
            "tropical",
            "light_background"
        };
    const std::array<String, 3>
        paneValues {
            "Monochrome Gray",
            "Tropical",
            "Light Background"
        };
    const std::array<int, 3>
        expectedActiveIndices {
            1,
            6,
            7
        };
    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (
        splitters.size(),
        3);

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".colour_scheme";
        auto* colourScheme =
            dynamic_cast<
                MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (
            colourScheme,
            nullptr)
            << id;
        EXPECT_EQ (
            colourScheme->getName(),
            "Colour scheme");
        EXPECT_EQ (
            colourScheme->getNumItems(),
            8);
        for (int choiceIndex = 0;
             choiceIndex < 8;
             ++choiceIndex)
        {
            EXPECT_EQ (
                colourScheme
                    ->getItemText (
                        choiceIndex),
                choices[choiceIndex]);
        }

        int choiceIndex = 0;
        for (PopupMenu::MenuItemIterator
                 iterator (
                     *colourScheme
                          ->getRootMenu(),
                     false);
             iterator.next();)
        {
            ASSERT_LT (
                choiceIndex,
                8);
            EXPECT_EQ (
                iterator
                    .getItem()
                    .accessibilityId,
                id
                    + ".choice."
                    + choiceIds[
                          choiceIndex]);
            ++choiceIndex;
        }
        EXPECT_EQ (choiceIndex, 8);

        auto* handler =
            colourScheme
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (displayNumber)
                + " colour scheme");
        const auto description =
            "Choose the trace and background colour scheme for LFP display "
            + String (displayNumber)
            + ": Classic, Monochrome Gray, Monochrome Yellow, Monochrome Purple, Monochrome Green, Open Ephys Logo, Tropical, or Light Background. This changes display rendering only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_FALSE (
            value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Classic");
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        showMenu));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        expand));
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        collapse));

        value->setValueAsString (
            paneValues[displayIndex]);
        auto* split =
            splitters[displayIndex];
        ASSERT_NE (split, nullptr);
        EXPECT_EQ (
            split->lfpDisplay
                ->getActiveColourSchemeIdx(),
            expectedActiveIndices[
                displayIndex]);
        EXPECT_EQ (
            colourScheme
                ->getSelectedId(),
            expectedActiveIndices[
                displayIndex]
                + 1);
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *colourScheme)
                ->isVisible(),
            displayIndex == 0);
    }

    std::vector<Label*> labels;
    collectLfpDescendants (
        *canvas,
        labels);
    EXPECT_EQ (
        std::count_if (
            labels.begin(),
            labels.end(),
            [] (const Label* label)
            {
                return label->getName()
                       == "ColourSchemeLabel";
            }),
        3);
    for (const auto* label : labels)
    {
        if (label->getName()
            == "ColourSchemeLabel")
        {
            EXPECT_FALSE (
                label->isAccessible());
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        ColourSchemeWorkerValueActionsAndTeardownStaySafe)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.colour_scheme";
    auto* colourScheme =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (colourScheme, nullptr);
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (
        splitters.size(),
        3);
    auto* split =
        splitters[0];
    ASSERT_NE (split, nullptr);
    auto retainedHandler =
        colourScheme
            ->createAccessibilityHandler();
    ASSERT_NE (
        retainedHandler,
        nullptr);
    auto* retainedValue =
        retainedHandler
            ->getValueInterface();
    ASSERT_NE (
        retainedValue,
        nullptr);
    const auto retainedActions =
        retainedHandler
            ->getActions();

    const auto writeFromWorker =
        [&] (StringRef newValue)
    {
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                retainedValue
                    ->setValueAsString (
                        String (newValue));
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
    };

    LfpThreadTrackingComboBoxListener
        listener;
    colourScheme->addListener (
        &listener);
    writeFromWorker (
        "Open Ephys Logo");
    colourScheme->removeListener (
        &listener);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_EQ (
        colourScheme->getSelectedId(),
        6);
    EXPECT_EQ (
        split->lfpDisplay
            ->getActiveColourSchemeIdx(),
        5);
    EXPECT_EQ (
        retainedValue
            ->getCurrentValueAsString(),
        "Open Ephys Logo");

    bool expandFound = false;
    std::atomic<bool>
        actionReturned { false };
    std::thread expandWorker (
        [&]
        {
            expandFound =
                retainedActions.invoke (
                    AccessibilityActionType::
                        expand);
            actionReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! actionReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        expandWorker,
        actionReturned);
    EXPECT_TRUE (expandFound);
    EXPECT_TRUE (
        colourScheme
            ->isPopupActive());

    actionReturned.store (false);
    bool collapseFound = false;
    std::thread collapseWorker (
        [&]
        {
            collapseFound =
                retainedActions.invoke (
                    AccessibilityActionType::
                        collapse);
            actionReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! actionReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        collapseWorker,
        actionReturned);
    EXPECT_TRUE (collapseFound);
    EXPECT_FALSE (
        colourScheme
            ->isPopupActive());

    colourScheme->setEnabled (false);
    writeFromWorker ("Classic");
    EXPECT_EQ (
        colourScheme->getSelectedId(),
        6);
    EXPECT_EQ (
        split->lfpDisplay
            ->getActiveColourSchemeIdx(),
        5);
    colourScheme->setEnabled (true);
    colourScheme
        ->synchroniseAccessibilityState();

    canvas.reset();
    std::atomic<bool>
        staleWorkerReturned { false };
    bool stalePressFound = false;
    bool staleShowMenuFound = false;
    std::thread staleWorker (
        [&]
        {
            retainedValue
                ->setValueAsString (
                    "Classic");
            stalePressFound =
                retainedActions.invoke (
                    AccessibilityActionType::
                        press);
            staleShowMenuFound =
                retainedActions.invoke (
                    AccessibilityActionType::
                        showMenu);
            staleWorkerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! staleWorkerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        staleWorkerReturned);
    EXPECT_EQ (
        retainedValue
            ->getCurrentValueAsString(),
        "Open Ephys Logo");
    EXPECT_TRUE (stalePressFound);
    EXPECT_TRUE (
        staleShowMenuFound);
}

TEST_F (LfpDisplayNodeTests,
        ColourSchemeValueSurvivesZeroChannelsBufferRemovalAndXmlRestore)
{
    auto zeroChannelTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (
            zeroChannelProcessor
                ->getNodeId())
        + ".lfp.display_1.colour_scheme";
    auto* colourScheme =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (colourScheme, nullptr);
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (
        splitters.size(),
        3);
    auto* split =
        splitters[0];
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *colourScheme);
    ASSERT_NE (split, nullptr);
    ASSERT_NE (options, nullptr);
    auto* value =
        colourScheme
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    value->setValueAsString (
        "Light Background");
    EXPECT_EQ (
        colourScheme->getSelectedId(),
        8);
    EXPECT_EQ (
        split->lfpDisplay
            ->getActiveColourSchemeIdx(),
        7);
    canvas->removeBufferForDisplay (0);
    value->setValueAsString (
        "Monochrome Gray");
    EXPECT_EQ (
        colourScheme->getSelectedId(),
        2);
    EXPECT_EQ (
        split->lfpDisplay
            ->getActiveColourSchemeIdx(),
        1);

    value->setValueAsString (
        "Tropical");
    XmlElement savedRoot ("ROOT");
    options->saveParameters (
        &savedRoot);
    auto* savedPane =
        savedRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (savedPane, nullptr);
    EXPECT_EQ (
        savedPane->getIntAttribute (
            "colourScheme"),
        7);

    value->setValueAsString (
        "Classic");
    LfpThreadTrackingComboBoxListener
        listener;
    colourScheme->addListener (
        &listener);
    options->loadParameters (
        &savedRoot);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "Tropical");
    EXPECT_EQ (
        split->lfpDisplay
            ->getActiveColourSchemeIdx(),
        6);

    const auto expectClassicFallback =
        [&] (XmlElement& root)
    {
        options->loadParameters (
            &root);
        EXPECT_EQ (
            colourScheme
                ->getSelectedId(),
            1);
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Classic");
        EXPECT_EQ (
            split->lfpDisplay
                ->getActiveColourSchemeIdx(),
            0);
        XmlElement canonicalRoot (
            "ROOT");
        options->saveParameters (
            &canonicalRoot);
        auto* canonicalPane =
            canonicalRoot.getChildByName (
                "LFPDISPLAY0");
        ASSERT_NE (
            canonicalPane,
            nullptr);
        EXPECT_EQ (
            canonicalPane
                ->getIntAttribute (
                    "colourScheme"),
            1);
    };

    XmlElement missingRoot (
        savedRoot);
    auto* missingPane =
        missingRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (missingPane, nullptr);
    missingPane->removeAttribute (
        "colourScheme");
    expectClassicFallback (
        missingRoot);

    XmlElement malformedRoot (
        savedRoot);
    auto* malformedPane =
        malformedRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (
        malformedPane,
        nullptr);
    malformedPane->setAttribute (
        "colourScheme",
        "not-a-number");
    expectClassicFallback (
        malformedRoot);

    for (const auto invalidId :
         { -1, 0, 9, 999 })
    {
        XmlElement invalidRoot (
            savedRoot);
        auto* invalidPane =
            invalidRoot.getChildByName (
                "LFPDISPLAY0");
        ASSERT_NE (
            invalidPane,
            nullptr);
        invalidPane->setAttribute (
            "colourScheme",
            invalidId);
        expectClassicFallback (
            invalidRoot);
    }
    colourScheme->removeListener (
        &listener);
    EXPECT_EQ (
        listener.callbackCount.load(),
        0);
}

TEST_F (LfpDisplayNodeTests,
        ExposesTrialAveragingToggleForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".trial_averaging";
        auto* button =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (button, nullptr)
            << id;
        EXPECT_NE (
            dynamic_cast<
                LfpViewer::
                    LfpOptionToggleButton*> (
                button),
            nullptr);

        auto* handler =
            button
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        const auto description =
            "Average successive TTL-triggered traces in LFP display "
            + String (displayNumber)
            + ". This takes effect only when a trigger source is selected; turning it on starts a new trial average. This changes display rendering only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::
                toggleButton);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (displayNumber)
                + " trial averaging");
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        EXPECT_TRUE (
            handler
                ->getCurrentState()
                .isCheckable());
        EXPECT_FALSE (
            handler
                ->getCurrentState()
                .isChecked());
        EXPECT_TRUE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        toggle));
        EXPECT_FALSE (
            handler->getActions()
                .contains (
                    AccessibilityActionType::
                        press));
        auto* value =
            handler
                ->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_TRUE (value->isReadOnly());
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "Off");

        auto* options =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *button);
        ASSERT_NE (options, nullptr);
        EXPECT_EQ (
            options->isVisible(),
            displayIndex == 0);
    }

    std::vector<Label*> labels;
    collectLfpDescendants (
        *canvas,
        labels);
    int averagingLabelCount = 0;
    for (const auto* label : labels)
    {
        if (label->getName()
            == "AverageSignalLabel")
        {
            ++averagingLabelCount;
            EXPECT_FALSE (
                label->isAccessible());
        }
    }
    EXPECT_EQ (averagingLabelCount, 3);
}

TEST_F (LfpDisplayNodeTests,
        TrialAveragingToggleRunsOnMessageThreadAndSurvivesEdgeCases)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.trial_averaging";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (button, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *button);
    ASSERT_NE (options, nullptr);
    auto* splitter =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (splitter, nullptr);
    EXPECT_FALSE (
        splitter->isAveraging());
    std::vector<UtilityButton*>
        utilityButtons;
    collectLfpDescendants (
        *options,
        utilityButtons);
    const auto resetIterator =
        std::find_if (
            utilityButtons.begin(),
            utilityButtons.end(),
            [] (UtilityButton* candidate)
            {
                return candidate->getLabel()
                       == "RESET";
            });
    ASSERT_NE (
        resetIterator,
        utilityButtons.end());
    auto* reset = *resetIterator;
    ASSERT_NE (reset, nullptr);
    EXPECT_FALSE (reset->isVisible());

    auto* liveHandler =
        button
            ->getAccessibilityHandler();
    ASSERT_NE (liveHandler, nullptr);
    auto* liveValue =
        liveHandler
            ->getValueInterface();
    ASSERT_NE (liveValue, nullptr);
    const auto retainedActions =
        liveHandler
            ->getActions();

    LfpThreadTrackingButtonListener
        listener;
    button->addListener (&listener);
    std::atomic<bool>
        workerReturned { false };
    bool toggled = false;
    std::thread worker (
        [&]
        {
            toggled =
                retainedActions.invoke (
                    AccessibilityActionType::
                        toggle);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || ! button
                          ->getToggleState());
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
    button->removeListener (&listener);

    EXPECT_TRUE (toggled);
    EXPECT_EQ (
        listener.callbackCount.load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        splitter->isAveraging());
    EXPECT_TRUE (
        liveHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        liveValue
            ->getCurrentValueAsString(),
        "On");
    EXPECT_TRUE (reset->isVisible());

    options->setAveraging (false);
    EXPECT_FALSE (
        button->getToggleState());
    EXPECT_FALSE (
        splitter->isAveraging());
    EXPECT_FALSE (
        liveHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        liveValue
            ->getCurrentValueAsString(),
        "Off");
    EXPECT_FALSE (reset->isVisible());

    button->setEnabled (false);
    workerReturned.store (false);
    std::thread disabledWorker (
        [&]
        {
            toggled =
                retainedActions.invoke (
                    AccessibilityActionType::
                        toggle);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        disabledWorker,
        workerReturned);
    EXPECT_TRUE (toggled);
    EXPECT_FALSE (
        button->getToggleState());
    EXPECT_FALSE (
        splitter->isAveraging());
    EXPECT_FALSE (
        liveHandler->isEnabled());

    button->setEnabled (true);
    canvas->removeBufferForDisplay (0);
    EXPECT_TRUE (
        retainedActions.invoke (
            AccessibilityActionType::
                toggle));
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        splitter->isAveraging());
    EXPECT_TRUE (reset->isVisible());

    auto zeroChannelTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto zeroCanvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    zeroCanvas->updateSettings();
    zeroCanvas->setSize (900, 800);
    zeroCanvas->addToDesktop (0);
    zeroCanvas->setVisible (true);
    zeroCanvas->toggleOptionsDrawer (true);
    const auto zeroId =
        "oe.processor."
        + String (
            zeroChannelProcessor
                ->getNodeId())
        + ".lfp.display_1.trial_averaging";
    auto* zeroButton =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *zeroCanvas,
                zeroId));
    ASSERT_NE (zeroButton, nullptr);
    auto* zeroSplitter =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (
            *zeroCanvas);
    ASSERT_NE (zeroSplitter, nullptr);
    auto zeroActions =
        zeroButton
            ->getAccessibilityHandler()
            ->getActions();
    EXPECT_TRUE (
        zeroActions.invoke (
            AccessibilityActionType::
                toggle));
    EXPECT_TRUE (
        zeroButton->getToggleState());
    EXPECT_TRUE (
        zeroSplitter->isAveraging());
    zeroCanvas
        ->removeBufferForDisplay (0);
    EXPECT_TRUE (
        zeroActions.invoke (
            AccessibilityActionType::
                toggle));
    EXPECT_FALSE (
        zeroButton->getToggleState());
    EXPECT_FALSE (
        zeroSplitter->isAveraging());

    canvas.reset();
    workerReturned.store (false);
    std::thread staleWorker (
        [&]
        {
            toggled =
                retainedActions.invoke (
                    AccessibilityActionType::
                        toggle);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
    EXPECT_TRUE (toggled);
}

TEST_F (LfpDisplayNodeTests,
        TrialAveragingXmlRoundTripsPerPaneWithoutNotifications)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3> buttons {};
    std::array<
        LfpViewer::LfpDisplayOptions*,
        3>
        options {};
    std::array<
        UtilityButton*,
        3>
        resetButtons {};
    std::array<
        AccessibilityValueInterface*,
        3>
        values {};
    std::array<
        LfpThreadTrackingButtonListener,
        3>
        listeners;
    std::vector<
        LfpViewer::LfpDisplaySplitter*>
        splitters;
    collectLfpDescendants (
        *canvas,
        splitters);
    ASSERT_EQ (splitters.size(), 3);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        buttons[displayIndex] =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            displayIndex + 1)
                        + ".trial_averaging"));
        ASSERT_NE (
            buttons[displayIndex],
            nullptr);
        options[displayIndex] =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *buttons[displayIndex]);
        ASSERT_NE (
            options[displayIndex],
            nullptr);
        values[displayIndex] =
            buttons[displayIndex]
                ->getAccessibilityHandler()
                ->getValueInterface();
        ASSERT_NE (
            values[displayIndex],
            nullptr);
        std::vector<UtilityButton*>
            utilityButtons;
        collectLfpDescendants (
            *options[displayIndex],
            utilityButtons);
        const auto resetIterator =
            std::find_if (
                utilityButtons.begin(),
                utilityButtons.end(),
                [] (
                    UtilityButton*
                        candidate)
                {
                    return candidate
                               ->getLabel()
                           == "RESET";
                });
        ASSERT_NE (
            resetIterator,
            utilityButtons.end());
        resetButtons[displayIndex] =
            *resetIterator;
        buttons[displayIndex]
            ->addListener (
                &listeners[displayIndex]);
        options[displayIndex]
            ->setAveraging (
                displayIndex != 1);
    }

    XmlElement savedRoot ("ROOT");
    for (auto* paneOptions : options)
        paneOptions->saveParameters (
            &savedRoot);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        auto* savedPane =
            savedRoot.getChildByName (
                "LFPDISPLAY"
                + String (displayIndex));
        ASSERT_NE (
            savedPane,
            nullptr);
        EXPECT_EQ (
            savedPane
                ->getBoolAttribute (
                    "trialAvg"),
            displayIndex != 1);
        options[displayIndex]
            ->setAveraging (false);
    }

    for (auto* paneOptions : options)
        paneOptions->loadParameters (
            &savedRoot);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const bool expected =
            displayIndex != 1;
        EXPECT_EQ (
            buttons[displayIndex]
                ->getToggleState(),
            expected);
        EXPECT_EQ (
            buttons[displayIndex]
                ->getAccessibilityHandler()
                ->getCurrentState()
                .isChecked(),
            expected);
        EXPECT_EQ (
            values[displayIndex]
                ->getCurrentValueAsString(),
            expected ? "On" : "Off");
        EXPECT_EQ (
            resetButtons[displayIndex]
                ->isVisible(),
            expected);
        EXPECT_EQ (
            splitters[displayIndex]
                ->isAveraging(),
            expected);
    }

    auto missingRoot =
        std::make_unique<XmlElement> (
            savedRoot);
    auto* missingPane =
        missingRoot
            ->getChildByName (
                "LFPDISPLAY0");
    ASSERT_NE (missingPane, nullptr);
    missingPane->removeAttribute (
        "trialAvg");
    options[0]->loadParameters (
        missingRoot.get());
    EXPECT_FALSE (
        buttons[0]->getToggleState());
    EXPECT_EQ (
        values[0]
            ->getCurrentValueAsString(),
        "Off");
    EXPECT_FALSE (
        resetButtons[0]->isVisible());
    EXPECT_FALSE (
        splitters[0]->isAveraging());

    auto malformedRoot =
        std::make_unique<XmlElement> (
            savedRoot);
    auto* malformedPane =
        malformedRoot
            ->getChildByName (
                "LFPDISPLAY0");
    ASSERT_NE (malformedPane, nullptr);
    malformedPane->setAttribute (
        "trialAvg",
        "garbage");
    options[0]->loadParameters (
        malformedRoot.get());
    EXPECT_FALSE (
        buttons[0]->getToggleState());
    EXPECT_EQ (
        values[0]
            ->getCurrentValueAsString(),
        "Off");
    EXPECT_FALSE (
        resetButtons[0]->isVisible());
    EXPECT_FALSE (
        splitters[0]->isAveraging());

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_EQ (
            listeners[displayIndex]
                .callbackCount.load(),
            0);
        buttons[displayIndex]
            ->removeListener (
                &listeners[displayIndex]);
    }
}

TEST_F (LfpDisplayNodeTests,
        ExposesResetTrialsActionForEveryPane)
{
    auto canvas = createAveragingCanvas();
    canvas->addToDesktop (0);
    canvas->toggleOptionsDrawer (true);
    const auto splitters = getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);
    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3> resetButtons {};
    std::array<
        LfpViewer::LfpDisplayOptions*,
        3>
        options {};

    for (int pane = 0; pane < 3; ++pane)
    {
        const auto displayNumber = pane + 1;
        const auto id =
            prefix + String (displayNumber)
            + ".reset_trials";
        resetButtons[pane] = dynamic_cast<Button*> (
            findLfpDescendantById (*canvas, id));
        ASSERT_NE (resetButtons[pane], nullptr) << id;
        EXPECT_EQ (resetButtons[pane]->getButtonText(), "RESET");
        EXPECT_FALSE (resetButtons[pane]->getClickingTogglesState());
        EXPECT_FALSE (resetButtons[pane]->getToggleState());
        EXPECT_FALSE (resetButtons[pane]->isVisible());

        auto* handler =
            resetButtons[pane]->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        const auto help =
            "Reset the accumulated trial average for LFP display "
            + String (displayNumber)
            + ". The next triggered view starts a new average. This changes display averaging only; the trigger source, acquisition, and recording are unaffected.";
        EXPECT_EQ (handler->getRole(), AccessibilityRole::button);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display " + String (displayNumber)
                + " reset trials");
        EXPECT_EQ (handler->getDescription(), help);
        EXPECT_EQ (handler->getHelp(), help);
        EXPECT_TRUE (handler->getActions().contains (
            AccessibilityActionType::press));
        EXPECT_FALSE (handler->getActions().contains (
            AccessibilityActionType::toggle));
        EXPECT_EQ (handler->getValueInterface(), nullptr);
        EXPECT_FALSE (
            handler->getCurrentState().isCheckable());
        EXPECT_FALSE (splitters[pane]->isAveraging());
        EXPECT_EQ (splitters[pane]->getTriggerChannel(), -1);
        options[pane] =
            findLfpAncestor<
                LfpViewer::LfpDisplayOptions> (
                *resetButtons[pane]);
        ASSERT_NE (options[pane], nullptr);
    }

    options[0]->setAveraging (true);
    EXPECT_TRUE (resetButtons[0]->isVisible());
    EXPECT_FALSE (resetButtons[1]->isVisible());
    EXPECT_FALSE (resetButtons[2]->isVisible());
    EXPECT_EQ (splitters[0]->getTriggerChannel(), -1);
    options[2]->setAveraging (true);
    EXPECT_TRUE (resetButtons[0]->isVisible());
    EXPECT_FALSE (resetButtons[1]->isVisible());
    EXPECT_TRUE (resetButtons[2]->isVisible());
    options[0]->setAveraging (false);
    EXPECT_FALSE (resetButtons[0]->isVisible());
    EXPECT_FALSE (resetButtons[1]->isVisible());
    EXPECT_TRUE (resetButtons[2]->isVisible());
}

TEST_F (LfpDisplayNodeTests,
        ResetTrialsActionRunsOnMessageThreadAndSurvivesLifecycle)
{
    auto canvas = createAveragingCanvas();
    canvas->addToDesktop (0);
    canvas->toggleOptionsDrawer (true);
    const auto splitters = getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);
    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.reset_trials";
    auto* reset = dynamic_cast<Button*> (
        findLfpDescendantById (*canvas, id));
    ASSERT_NE (reset, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (*reset);
    ASSERT_NE (options, nullptr);
    options->setAveraging (true);
    ASSERT_TRUE (reset->isShowing());

    LfpThreadTrackingButtonListener listener;
    reset->addListener (&listener);
    const auto retainedActions =
        reset->getAccessibilityHandler()->getActions();
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    pumpUntilButtonCallbacks (listener, 1);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());

    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    pumpUntilButtonCallbacks (listener, 3);
    EXPECT_EQ (listener.callbackCount.load(), 3);
    EXPECT_TRUE (splitters[0]->isAveraging());
    EXPECT_EQ (splitters[0]->getTriggerChannel(), -1);

    reset->setEnabled (false);
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.callbackCount.load(), 3);

    reset->setEnabled (true);
    options->setAveraging (false);
    EXPECT_FALSE (reset->isShowing());
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.callbackCount.load(), 3);

    reset->removeListener (&listener);
    canvas.reset();
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.callbackCount.load(), 3);
}

TEST_F (LfpDisplayNodeTests,
        ResetTrialsActionSurvivesMissingDisplayBuffers)
{
    auto zeroChannelTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0, sampleRate, bitVolts }));
    auto* zeroProcessor =
        zeroChannelTester
            ->createProcessor<LfpViewer::LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<LfpViewer::LfpDisplayCanvas> (
            zeroProcessor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 600);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    auto splitters = getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* reset = dynamic_cast<Button*> (
        findLfpDescendantById (
            *canvas,
            "oe.processor."
                + String (zeroProcessor->getNodeId())
                + ".lfp.display_1.reset_trials"));
    ASSERT_NE (reset, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (*reset);
    ASSERT_NE (options, nullptr);
    options->setAveraging (true);
    LfpThreadTrackingButtonListener listener;
    reset->addListener (&listener);
    const auto actions =
        reset->getAccessibilityHandler()->getActions();

    EXPECT_TRUE (invokeLfpActionFromWorker (
        actions, AccessibilityActionType::press));
    pumpUntilButtonCallbacks (listener, 1);
    canvas->removeBufferForDisplay (0);
    EXPECT_TRUE (invokeLfpActionFromWorker (
        actions, AccessibilityActionType::press));
    pumpUntilButtonCallbacks (listener, 2);
    EXPECT_EQ (listener.callbackCount.load(), 2);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
    EXPECT_TRUE (splitters[0]->isAveraging());
    EXPECT_EQ (splitters[0]->getTriggerChannel(), -1);
}

TEST_F (LfpDisplayNodeTests,
        ResetTrialsIsNotPersistedAndVisibilityFollowsTrialAveraging)
{
    auto canvas = createAveragingCanvas();
    const auto splitters = getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);
    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.reset_trials";
    auto* reset = dynamic_cast<Button*> (
        findLfpDescendantById (*canvas, id));
    ASSERT_NE (reset, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (*reset);
    ASSERT_NE (options, nullptr);
    options->setAveraging (true);

    XmlElement savedRoot ("ROOT");
    options->saveParameters (&savedRoot);
    auto* savedPane =
        savedRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (savedPane, nullptr);
    EXPECT_TRUE (savedPane->getBoolAttribute ("trialAvg"));
    EXPECT_FALSE (savedPane->hasAttribute ("resetTrials"));
    EXPECT_FALSE (savedPane->hasAttribute ("reset_trials"));
    options->setAveraging (false);
    options->loadParameters (&savedRoot);
    EXPECT_TRUE (splitters[0]->isAveraging());
    EXPECT_TRUE (reset->isVisible());

    savedPane->removeAttribute ("trialAvg");
    options->loadParameters (&savedRoot);
    EXPECT_FALSE (splitters[0]->isAveraging());
    EXPECT_FALSE (reset->isVisible());
    savedPane->setAttribute ("trialAvg", "garbage");
    options->loadParameters (&savedRoot);
    EXPECT_FALSE (splitters[0]->isAveraging());
    EXPECT_FALSE (reset->isVisible());
}

TEST_F (LfpDisplayNodeTests,
        ResetTrialsActionRecoversWhenAncestorsBecomeShowing)
{
    auto canvas = createAveragingCanvas();
    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.reset_trials";
    auto* reset = dynamic_cast<Button*> (
        findLfpDescendantById (*canvas, id));
    ASSERT_NE (reset, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (*reset);
    ASSERT_NE (options, nullptr);

    XmlElement restoredRoot ("ROOT");
    options->saveParameters (&restoredRoot);
    auto* restoredPane =
        restoredRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (restoredPane, nullptr);
    restoredPane->setAttribute ("trialAvg", true);
    options->loadParameters (&restoredRoot);
    ASSERT_TRUE (reset->isVisible());
    ASSERT_FALSE (reset->isShowing());

    canvas->addToDesktop (0);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (reset->isShowing());
    auto* handler = reset->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto retainedActions = handler->getActions();
    LfpThreadTrackingButtonListener listener;
    reset->addListener (&listener);
    EXPECT_TRUE (handler->isEnabled());
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    pumpUntilButtonCallbacks (listener, 1);
    EXPECT_EQ (listener.callbackCount.load(), 1);

    canvas->setVisible (false);
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    canvas->setVisible (true);
    ASSERT_TRUE (reset->isShowing());
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    pumpUntilButtonCallbacks (listener, 2);
    EXPECT_EQ (listener.callbackCount.load(), 2);

    canvas->toggleOptionsDrawer (false);
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.callbackCount.load(), 2);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (reset->isShowing());
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    pumpUntilButtonCallbacks (listener, 3);
    EXPECT_EQ (listener.callbackCount.load(), 3);

    reset->setEnabled (false);
    EXPECT_FALSE (handler->isEnabled());
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.callbackCount.load(), 3);
}

TEST_F (LfpDisplayNodeTests,
        ResetTrialsActionRejectsClippedAncestorsAndRecovers)
{
    auto canvas = createAveragingCanvas();
    canvas->addToDesktop (0);
    canvas->toggleOptionsDrawer (true);
    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.reset_trials";
    auto* reset = dynamic_cast<Button*> (
        findLfpDescendantById (*canvas, id));
    ASSERT_NE (reset, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (*reset);
    ASSERT_NE (options, nullptr);
    options->setAveraging (true);
    ASSERT_TRUE (reset->isShowing());
    const auto retainedActions =
        reset->getAccessibilityHandler()->getActions();
    LfpThreadTrackingButtonListener listener;
    reset->addListener (&listener);

    canvas->toggleOptionsDrawer (false);
    ASSERT_TRUE (reset->isShowing());
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.callbackCount.load(), 0);

    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (reset->isShowing());
    EXPECT_TRUE (invokeLfpActionFromWorker (
        retainedActions, AccessibilityActionType::press));
    pumpUntilButtonCallbacks (listener, 1);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
}

TEST_F (LfpDisplayNodeTests,
        BeginAnimationResetsTrialAveragingWithoutDisplayBuffer)
{
    auto canvas = createAveragingCanvas();
    const auto splitters = getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* split = splitters[0];
    ASSERT_NE (split, nullptr);
    auto* reset = getResetTrialsButton (*split);
    ASSERT_NE (reset, nullptr);

    beginTriggeredAveraging (*canvas, { split });

    startTrial (*canvas, 10.0f);
    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 10.0f, 0.001f);
    finishTriggeredView (*canvas, 10.0f);

    startTrial (*canvas, 20.0f);
    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 15.0f, 0.001f);
    finishTriggeredView (*canvas, 20.0f);

    auto* displayBuffer = split->displayBuffer;
    ASSERT_NE (displayBuffer, nullptr);
    canvas->removeBufferForDisplay (0);
    canvas->beginAnimation();
    canvas->endAnimation();
    split
        ->streamSelection
        ->setEnabled (
            true);
    ASSERT_TRUE (
        split
            ->selectStreamByKey (
                displayBuffer
                    ->streamKey));
    canvas->refreshState();

    processConstantBlock (40.0f, 1024);
    processTriggeredBlock (40.0f);
    const auto indexBeforeRestart = split->screenBufferIndex[0];
    canvas->refreshState();
    EXPECT_EQ (indexBeforeRestart, 0);
    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 40.0f, 0.001f);
    finishTriggeredView (*canvas, 40.0f);

    startTrial (*canvas, 60.0f);
    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 50.0f, 0.001f);
    finishTriggeredView (*canvas, 60.0f);

    canvas->removeBufferForDisplay (0);
    split->options->buttonClicked (reset);
    canvas->beginAnimation();
    canvas->endAnimation();
    split
        ->streamSelection
        ->setEnabled (
            true);
    ASSERT_TRUE (
        split
            ->selectStreamByKey (
                displayBuffer
                    ->streamKey));
    canvas->refreshState();

    startTrial (*canvas, 80.0f);
    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 80.0f, 0.001f);
    finishTriggeredView (*canvas, 80.0f);

    startTrial (*canvas, 100.0f);
    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 90.0f, 0.001f);

    processor->stopAcquisition();
}

TEST_F (LfpDisplayNodeTests,
        ResetTrialsPreservesCurrentTrialWeightUntilNextTrigger)
{
    auto canvas = createAveragingCanvas();
    const auto splitters = getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* split = splitters[0];
    ASSERT_NE (split, nullptr);
    auto* reset = getResetTrialsButton (*split);
    ASSERT_NE (reset, nullptr);
    beginTriggeredAveraging (*canvas, { split });

    startTrial (*canvas, 10.0f);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 10.0f, 0.001f);
    finishTriggeredView (*canvas, 10.0f);
    startTrial (*canvas, 20.0f);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 15.0f, 0.001f);
    finishTriggeredView (*canvas, 20.0f);

    startTrial (*canvas, 40.0f);
    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (
        split->getYCoordMean (0, 100),
        (10.0f + 20.0f + 40.0f) / 3.0f,
        0.001f);
    const auto indexBeforeReset = split->screenBufferIndex[0];
    split->options->buttonClicked (reset);
    split->options->buttonClicked (reset);
    EXPECT_EQ (split->screenBufferIndex[0], indexBeforeReset);

    processConstantBlock (40.0f);
    canvas->refreshState();
    ASSERT_GT (split->screenBufferIndex[0], indexBeforeReset);
    const auto continuationSample = indexBeforeReset + 10;
    ASSERT_LT (continuationSample, split->screenBufferIndex[0]);
    EXPECT_TRUE (std::isfinite (
        split->getYCoordMean (0, continuationSample)));
    EXPECT_NEAR (
        split->getYCoordMean (0, continuationSample),
        (10.0f + 20.0f + 40.0f) / 3.0f,
        0.001f);

    finishTriggeredView (*canvas, 40.0f);
    processConstantBlock (70.0f, 1024);
    processTriggeredBlock (70.0f);
    const auto completedViewIndex = split->screenBufferIndex[0];
    canvas->refreshState();
    ASSERT_GT (completedViewIndex, split->screenBufferIndex[0]);
    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 70.0f, 0.001f);

    processor->stopAcquisition();
}

TEST_F (LfpDisplayNodeTests,
        ResetTrialsPendingSurvivesContinuousMode)
{
    auto canvas = createAveragingCanvas();
    const auto splitters = getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* split = splitters[0];
    auto* reset = getResetTrialsButton (*split);
    ASSERT_NE (reset, nullptr);
    beginTriggeredAveraging (*canvas, { split });

    startTrial (*canvas, 20.0f);
    finishTriggeredView (*canvas, 20.0f);
    split->options->setTriggerSourceSelection (1);
    ASSERT_EQ (split->getTriggerChannel(), -1);
    split->options->buttonClicked (reset);
    split->options->buttonClicked (reset);
    processConstantBlock (60.0f, 1024);
    split->options->setTriggerSourceSelection (2);
    processTriggeredBlock (60.0f);
    canvas->refreshState();

    ASSERT_GT (split->screenBufferIndex[0], 100);
    EXPECT_NEAR (split->getYCoordMean (0, 100), 60.0f, 0.001f);
    processor->stopAcquisition();
}

TEST_F (LfpDisplayNodeTests,
        ResetTrialsIsPaneLocal)
{
    auto canvas = createAveragingCanvas (LfpViewer::SplitLayouts::THREE_HORZ);
    const auto splitters = getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);
    auto* reset = getResetTrialsButton (*splitters[0]);
    ASSERT_NE (reset, nullptr);
    beginTriggeredAveraging (*canvas, splitters);

    startTrial (*canvas, 10.0f);
    finishTriggeredView (*canvas, 10.0f);
    startTrial (*canvas, 20.0f);
    finishTriggeredView (*canvas, 20.0f);
    startTrial (*canvas, 40.0f);
    for (auto* split : splitters)
    {
        ASSERT_GT (split->screenBufferIndex[0], 100);
        EXPECT_NEAR (
            split->getYCoordMean (0, 100),
            (10.0f + 20.0f + 40.0f) / 3.0f,
            0.001f);
    }
    splitters[0]->options->buttonClicked (reset);
    splitters[0]->options->buttonClicked (reset);
    finishTriggeredView (*canvas, 40.0f);

    processConstantBlock (70.0f, 1024);
    processTriggeredBlock (70.0f);
    splitters[0]->setVisible (false);
    canvas->refreshState();
    splitters[0]->setVisible (true);
    canvas->refreshState();

    EXPECT_NEAR (splitters[0]->getYCoordMean (0, 100), 70.0f, 0.001f);
    for (int pane = 1; pane < 3; ++pane)
    {
        ASSERT_GT (splitters[pane]->screenBufferIndex[0], 100);
        EXPECT_NEAR (
            splitters[pane]->getYCoordMean (0, 100),
            (10.0f + 20.0f + 40.0f + 70.0f) / 4.0f,
            0.001f);
    }
    processor->stopAcquisition();
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaRetainedResetTrialsInvokeHonoursDrawerClipping)
{
    auto canvas = createAveragingCanvas();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->toggleOptionsDrawer (true);
    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.reset_trials";
    auto* reset = dynamic_cast<Button*> (
        findLfpDescendantById (*canvas, id));
    ASSERT_NE (reset, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (*reset);
    ASSERT_NE (options, nullptr);
    options->setAveraging (true);
    ASSERT_TRUE (reset->isShowing());
    LfpThreadTrackingButtonListener listener;
    reset->addListener (&listener);
    const auto window =
        static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);
    LfpRetainedWindowsInvokeSession retained (
        window,
        std::wstring (
            id.toWideCharPointer()));
    for (int attempt = 0;
         attempt < 100 && ! retained.isReady();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    ASSERT_TRUE (retained.isReady());
    ASSERT_EQ (retained.getSetupResult(), S_OK);

    const auto waitForInvoke =
        [&]
    {
        for (int attempt = 0;
             attempt < 100
                 && ! retained
                         .hasInvokeCompleted();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        ASSERT_TRUE (
            retained.hasInvokeCompleted());
    };

    canvas->toggleOptionsDrawer (false);
    ASSERT_TRUE (reset->isShowing());
    retained.requestInvoke();
    waitForInvoke();
    EXPECT_EQ (
        retained.getInvokeResult(),
        S_OK);
    MessageManager::getInstance()
        ->runDispatchLoopUntil (20);
    EXPECT_EQ (listener.callbackCount.load(), 0);

    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (reset->isShowing());
    retained.requestInvoke();
    waitForInvoke();
    EXPECT_EQ (
        retained.getInvokeResult(),
        S_OK);
    pumpUntilButtonCallbacks (
        listener,
        1);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaResetTrialsRecoversAfterOffscreenRestore)
{
    auto canvas = createAveragingCanvas();
    canvas->setSize (1200, 800);
    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.reset_trials";
    auto* reset = dynamic_cast<Button*> (
        findLfpDescendantById (*canvas, id));
    ASSERT_NE (reset, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (*reset);
    ASSERT_NE (options, nullptr);

    XmlElement restoredRoot ("ROOT");
    options->saveParameters (&restoredRoot);
    auto* restoredPane =
        restoredRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (restoredPane, nullptr);
    restoredPane->setAttribute ("trialAvg", true);
    options->loadParameters (&restoredRoot);
    ASSERT_TRUE (reset->isVisible());
    ASSERT_FALSE (reset->isShowing());

    LfpThreadTrackingButtonListener listener;
    reset->addListener (&listener);
    canvas->addToDesktop (0);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (reset->isShowing());
    const auto window =
        static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto available =
        invokeWindowsUiaFromWorker (
            window,
            id,
            LfpWindowsUiaAction::queryInvoke);
    EXPECT_EQ (available.invokeResult, S_OK);
    EXPECT_EQ (available.enabled, TRUE);
    EXPECT_TRUE (available.invokePatternAvailable);
    EXPECT_EQ (
        invokeWindowsUiaFromWorker (
            window,
            id,
            LfpWindowsUiaAction::invoke)
            .invokeResult,
        S_OK);
    pumpUntilButtonCallbacks (listener, 1);
    EXPECT_EQ (listener.callbackCount.load(), 1);

    canvas->toggleOptionsDrawer (false);
    EXPECT_EQ (
        invokeWindowsUiaFromWorker (
            window,
            id,
            LfpWindowsUiaAction::queryInvoke)
            .invokeResult,
        E_FAIL);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (reset->isShowing());
    EXPECT_EQ (
        invokeWindowsUiaFromWorker (
            window,
            id,
            LfpWindowsUiaAction::invoke)
            .invokeResult,
        S_OK);
    pumpUntilButtonCallbacks (listener, 2);
    EXPECT_EQ (listener.callbackCount.load(), 2);

    reset->setEnabled (false);
    EXPECT_EQ (
        invokeWindowsUiaFromWorker (
            window,
            id,
            LfpWindowsUiaAction::invoke)
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_EQ (listener.callbackCount.load(), 2);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaInvokesResetTrialsAsAButton)
{
    auto canvas = createAveragingCanvas();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());
    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.reset_trials";
    auto* reset = dynamic_cast<Button*> (
        findLfpDescendantById (*canvas, id));
    ASSERT_NE (reset, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (*reset);
    ASSERT_NE (options, nullptr);
    auto* split =
        findLfpDescendant<LfpViewer::LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (split, nullptr);
    options->setAveraging (true);
    ASSERT_TRUE (reset->isShowing());
    ASSERT_EQ (split->getTriggerChannel(), -1);
    LfpThreadTrackingButtonListener listener;
    reset->addListener (&listener);
    const auto window =
        static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto runAction =
        [&] (LfpWindowsUiaAction action)
    {
        LfpWindowsUiaInvokeResult result;
        std::atomic<bool> workerReturned { false };
        std::thread worker (
            [&]
            {
                result = invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (
                        id.toWideCharPointer()),
                    action);
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return result;
    };

    const auto initial =
        runAction (LfpWindowsUiaAction::queryInvoke);
    EXPECT_EQ (initial.invokeResult, S_OK);
    EXPECT_EQ (
        initial.controlType,
        UIA_ButtonControlTypeId);
    EXPECT_EQ (initial.enabled, TRUE);
    EXPECT_EQ (
        initial.name,
        L"LFP display 1 reset trials");
    EXPECT_EQ (
        initial.help,
        L"Reset the accumulated trial average for LFP display 1. The next triggered view starts a new average. This changes display averaging only; the trigger source, acquisition, and recording are unaffected.");
    EXPECT_TRUE (initial.invokePatternAvailable);
    EXPECT_FALSE (initial.togglePatternAvailable);
    EXPECT_FALSE (initial.valuePatternAvailable);

    const auto invoked =
        runAction (LfpWindowsUiaAction::invoke);
    EXPECT_EQ (invoked.invokeResult, S_OK);
    pumpUntilButtonCallbacks (listener, 1);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
    EXPECT_TRUE (split->isAveraging());
    EXPECT_EQ (split->getTriggerChannel(), -1);

    reset->setEnabled (false);
    const auto disabled =
        runAction (LfpWindowsUiaAction::invoke);
    EXPECT_EQ (
        disabled.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_EQ (listener.callbackCount.load(), 1);

    reset->setEnabled (true);
    options->setAveraging (false);
    EXPECT_EQ (
        runAction (
            LfpWindowsUiaAction::queryInvoke)
            .invokeResult,
        E_FAIL);
    options->setAveraging (true);
    canvas->toggleOptionsDrawer (false);
    EXPECT_EQ (
        runAction (
            LfpWindowsUiaAction::queryInvoke)
            .invokeResult,
        E_FAIL);
    canvas->toggleOptionsDrawer (true);
    canvas->setVisible (false);
    canvas->removeFromDesktop();
    EXPECT_TRUE (
        FAILED (
            runAction (
                LfpWindowsUiaAction::queryInvoke)
                .invokeResult));
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesTrialAveraging)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.trial_averaging";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (button, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *button);
    ASSERT_NE (options, nullptr);
    auto* splitter =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (splitter, nullptr);
    std::vector<UtilityButton*>
        utilityButtons;
    collectLfpDescendants (
        *options,
        utilityButtons);
    const auto resetIterator =
        std::find_if (
            utilityButtons.begin(),
            utilityButtons.end(),
            [] (UtilityButton* candidate)
            {
                return candidate->getLabel()
                       == "RESET";
            });
    ASSERT_NE (
        resetIterator,
        utilityButtons.end());
    auto* reset = *resetIterator;

    const auto window =
        static_cast<HWND> (
            canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);
    const auto runAction =
        [&] (StringRef targetId,
             LfpWindowsUiaAction action)
    {
        LfpWindowsUiaInvokeResult result;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                result =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (targetId)
                                .toWideCharPointer()),
                        action);
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return result;
    };

    const auto description =
        L"Average successive TTL-triggered traces in LFP display 1. This takes effect only when a trigger source is selected; turning it on starts a new trial average. This changes display rendering only; acquisition and recording are unaffected.";
    const auto initial =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryToggle);
    EXPECT_EQ (
        initial.invokeResult,
        S_OK);
    EXPECT_EQ (
        initial.controlType,
        UIA_CheckBoxControlTypeId);
    EXPECT_EQ (
        initial.enabled,
        TRUE);
    EXPECT_EQ (
        initial.name,
        L"LFP display 1 trial averaging");
    EXPECT_EQ (
        initial.help,
        description);
    EXPECT_TRUE (
        initial.togglePatternAvailable);
    EXPECT_EQ (
        initial.toggleState,
        ToggleState_Off);
    EXPECT_TRUE (
        initial.valuePatternAvailable);
    EXPECT_EQ (
        initial.valueReadOnly,
        TRUE);
    EXPECT_EQ (
        initial.value,
        L"Off");
    EXPECT_FALSE (
        initial.invokePatternAvailable);

    const auto enabled =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle);
    EXPECT_EQ (
        enabled.invokeResult,
        S_OK);
    EXPECT_EQ (
        enabled.toggleState,
        ToggleState_On);
    EXPECT_EQ (
        enabled.value,
        L"On");
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        splitter->isAveraging());
    EXPECT_TRUE (reset->isVisible());

    button->setEnabled (false);
    const auto disabled =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle);
    EXPECT_EQ (
        disabled.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (
        button->getToggleState());

    const auto hidden =
        runAction (
            prefix
                + "2.trial_averaging",
            LfpWindowsUiaAction::
                queryToggle);
    EXPECT_EQ (
        hidden.invokeResult,
        E_FAIL);

    button->setEnabled (true);
    canvas->toggleOptionsDrawer (
        false);
    const auto closed =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryToggle);
    EXPECT_EQ (
        closed.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWritesAndSelectsTriggerSource)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.trigger_source";
    auto* triggerSource =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* split =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (triggerSource, nullptr);
    ASSERT_NE (split, nullptr);
    const auto window =
        static_cast<HWND> (
            canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto runAction =
        [&] (StringRef targetId,
             LfpWindowsUiaAction action,
             StringRef newValue = {})
    {
        LfpWindowsUiaInvokeResult result;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                result =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (targetId)
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (newValue)
                                .toWideCharPointer()));
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return result;
    };

    const auto description =
        L"Choose the TTL line that triggers LFP display 1: None uses continuous scrolling; 1 through 16 starts each triggered view from the corresponding TTL event line. This changes display timing only; acquisition and recording are unaffected.";
    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 trigger source");
    EXPECT_EQ (
        initialResult.help,
        description);
    EXPECT_TRUE (
        initialResult
            .valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        FALSE);
    EXPECT_TRUE (
        initialResult
            .expandCollapsePatternAvailable);
    EXPECT_TRUE (
        initialResult
            .invokePatternAvailable);
    EXPECT_EQ (
        initialResult.value,
        L"None");

    const auto setValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            "8");
    EXPECT_EQ (
        setValueResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        setValueResult.value,
        L"8");
    EXPECT_EQ (
        split->getTriggerChannel(),
        7);

    const auto expandResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        expandResult.focusResult,
        S_OK);
    EXPECT_EQ (
        expandResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        expandResult.expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (
        triggerSource
            ->isPopupActive());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (50);
    const auto choiceId =
        id
        + ".choice.line_16";
    const auto selectResult =
        runAction (
            choiceId,
            LfpWindowsUiaAction::
                select);
    MessageManager::getInstance()
        ->runDispatchLoopUntil (50);
    EXPECT_EQ (
        selectResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        selectResult.controlType,
        UIA_MenuItemControlTypeId);
    EXPECT_TRUE (
        selectResult
            .selectionItemPatternAvailable);
    EXPECT_EQ (
        triggerSource
            ->getSelectedId(),
        17);
    EXPECT_EQ (
        split->getTriggerChannel(),
        15);

    const auto reexpandedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        reexpandedResult.invokeResult,
        S_OK);
    const auto selectedChoiceResult =
        runAction (
            choiceId,
            LfpWindowsUiaAction::
                querySelection);
    EXPECT_EQ (
        selectedChoiceResult.invokeResult,
        S_OK);
    EXPECT_TRUE (
        selectedChoiceResult.selected);
    const auto collapseResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                collapse);
    EXPECT_EQ (
        collapseResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        collapseResult.expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_FALSE (
        triggerSource
            ->isPopupActive());

    triggerSource->setEnabled (false);
    const auto disabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            "None");
    EXPECT_EQ (
        disabledResult.invokeResult,
        UIA_E_ELEMENTNOTENABLED);
    EXPECT_EQ (
        split->getTriggerChannel(),
        15);
    triggerSource->setEnabled (true);

    const auto hiddenPaneResult =
        runAction (
            prefix
                + "2.trigger_source",
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);

    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        closedDrawerResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWritesAndSelectsColourScheme)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.colour_scheme";
    auto* colourScheme =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (colourScheme, nullptr);
    const auto splitters =
        getDisplaySplitters (
            *canvas);
    ASSERT_EQ (
        splitters.size(),
        3);
    auto* split =
        splitters[0];
    ASSERT_NE (split, nullptr);
    const auto window =
        static_cast<HWND> (
            canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto runAction =
        [&] (StringRef targetId,
             LfpWindowsUiaAction action,
             StringRef newValue = {})
    {
        LfpWindowsUiaInvokeResult
            result;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                result =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (targetId)
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (newValue)
                                .toWideCharPointer()));
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return result;
    };

    const auto description =
        L"Choose the trace and background colour scheme for LFP display 1: Classic, Monochrome Gray, Monochrome Yellow, Monochrome Purple, Monochrome Green, Open Ephys Logo, Tropical, or Light Background. This changes display rendering only; acquisition and recording are unaffected.";
    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 colour scheme");
    EXPECT_EQ (
        initialResult.help,
        description);
    EXPECT_TRUE (
        initialResult
            .valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        FALSE);
    EXPECT_TRUE (
        initialResult
            .expandCollapsePatternAvailable);
    EXPECT_TRUE (
        initialResult
            .invokePatternAvailable);
    EXPECT_EQ (
        initialResult.value,
        L"Classic");

    const auto setValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            "Tropical");
    EXPECT_EQ (
        setValueResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        setValueResult.value,
        L"Tropical");
    EXPECT_EQ (
        colourScheme->getSelectedId(),
        7);
    EXPECT_EQ (
        split->lfpDisplay
            ->getActiveColourSchemeIdx(),
        6);

    const auto expandResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        expandResult.focusResult,
        S_OK);
    EXPECT_EQ (
        expandResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        expandResult.expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (
        colourScheme
            ->isPopupActive());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (50);
    const auto choiceId =
        id
        + ".choice.light_background";
    const auto selectResult =
        runAction (
            choiceId,
            LfpWindowsUiaAction::
                select);
    MessageManager::getInstance()
        ->runDispatchLoopUntil (50);
    EXPECT_EQ (
        selectResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        selectResult.controlType,
        UIA_MenuItemControlTypeId);
    EXPECT_TRUE (
        selectResult
            .selectionItemPatternAvailable);
    EXPECT_EQ (
        colourScheme->getSelectedId(),
        8);
    EXPECT_EQ (
        split->lfpDisplay
            ->getActiveColourSchemeIdx(),
        7);

    const auto reexpandedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        reexpandedResult.invokeResult,
        S_OK);
    const auto selectedChoiceResult =
        runAction (
            choiceId,
            LfpWindowsUiaAction::
                querySelection);
    EXPECT_EQ (
        selectedChoiceResult
            .invokeResult,
        S_OK);
    EXPECT_TRUE (
        selectedChoiceResult.selected);
    const auto collapseResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                collapse);
    EXPECT_EQ (
        collapseResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        collapseResult.expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_FALSE (
        colourScheme
            ->isPopupActive());

    colourScheme->setEnabled (false);
    const auto disabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            "Classic");
    EXPECT_EQ (
        disabledResult.invokeResult,
        UIA_E_ELEMENTNOTENABLED);
    EXPECT_EQ (
        split->lfpDisplay
            ->getActiveColourSchemeIdx(),
        7);
    colourScheme->setEnabled (true);

    const auto hiddenPaneResult =
        runAction (
            prefix
                + "2.colour_scheme",
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);

    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        closedDrawerResult.invokeResult,
        S_OK);

    canvas->removeFromDesktop();
    const auto closedWindowResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        closedWindowResult.invokeResult,
        UIA_E_ELEMENTNOTAVAILABLE);
}
#endif

TEST_F (LfpDisplayNodeTests,
        SpikeRasterWorkerValuesCanonicaliseAndKeepOffsetOwnershipCoherent)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.";
    auto* spikeRaster =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                prefix + "spike_raster"));
    auto* subtractOffset =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix + "subtract_offset"));
    auto* display =
        findLfpDescendant<
            LfpViewer::LfpDisplay> (
            *canvas);
    ASSERT_NE (spikeRaster, nullptr);
    ASSERT_NE (subtractOffset, nullptr);
    ASSERT_NE (display, nullptr);
    auto* value =
        spikeRaster
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    const auto writeFromWorker =
        [&] (StringRef newValue)
    {
        std::atomic<bool> workerReturned { false };
        std::thread worker (
            [&]
            {
                value->setValueAsString (
                    String (newValue));
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
    };

    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    LfpThreadTrackingComboBoxListener listener;
    spikeRaster->addListener (&listener);
    writeFromWorker ("125");
    spikeRaster->removeListener (&listener);
    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
    EXPECT_EQ (spikeRaster->getText(), "-125");
    EXPECT_EQ (value->getCurrentValueAsString(), "-125");
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -125.0f);
    EXPECT_TRUE (subtractOffset->getToggleState());

    writeFromWorker ("OFF");
    EXPECT_EQ (spikeRaster->getText(), "OFF");
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_FALSE (subtractOffset->getToggleState());
    writeFromWorker ("-100");
    EXPECT_EQ (spikeRaster->getText(), "-100");
    EXPECT_EQ (value->getCurrentValueAsString(), "-100");
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -100.0f);
    EXPECT_TRUE (subtractOffset->getToggleState());
    writeFromWorker ("OFF");
    EXPECT_EQ (spikeRaster->getText(), "OFF");
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_FALSE (subtractOffset->getToggleState());

    for (const auto& testCase :
         std::array<std::pair<String, String>, 4> {
             std::make_pair ("-125", "-125"),
             std::make_pair ("600", "-500"),
             std::make_pair ("0", "OFF"),
             std::make_pair ("invalid", "OFF") })
    {
        writeFromWorker (testCase.first);
        EXPECT_EQ (spikeRaster->getText(), testCase.second);
        EXPECT_EQ (
            value->getCurrentValueAsString(),
            testCase.second);
        EXPECT_EQ (
            display->getSpikeRasterPlotting(),
            testCase.second != "OFF");
    }
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -500.0f);
    EXPECT_FALSE (subtractOffset->getToggleState());

    const auto retainedActions =
        spikeRaster
            ->getAccessibilityHandler()
            ->getActions();
    auto retainedHandler =
        spikeRaster
            ->createAccessibilityHandler();
    ASSERT_NE (retainedHandler, nullptr);
    auto* retainedValue =
        retainedHandler
            ->getValueInterface();
    ASSERT_NE (retainedValue, nullptr);
    spikeRaster->setEnabled (false);
    writeFromWorker ("-50");
    EXPECT_EQ (spikeRaster->getText(), "OFF");
    canvas.reset();
    retainedValue->setValueAsString ("-50");
    EXPECT_EQ (
        retainedValue->getCurrentValueAsString(),
        "OFF");
    std::atomic<bool> workerReturned { false };
    bool staleActionFound = false;
    std::thread staleWorker (
        [&]
        {
            staleActionFound =
                retainedActions.invoke (
                    AccessibilityActionType::press);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
    EXPECT_TRUE (staleActionFound);
}

TEST_F (LfpDisplayNodeTests,
        SpikeRasterAccessibilityValueSurvivesZeroChannelsBufferRemovalAndXmlRestore)
{
    auto zeroChannelTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<LfpViewer::LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (zeroChannelProcessor->getNodeId())
        + ".lfp.display_1.spike_raster";
    auto* spikeRaster =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    auto* display =
        findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (spikeRaster, nullptr);
    ASSERT_NE (display, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (
            *spikeRaster);
    ASSERT_NE (options, nullptr);
    auto* value =
        spikeRaster
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    value->setValueAsString ("-100");
    EXPECT_EQ (value->getCurrentValueAsString(), "-100");
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -100.0f);
    canvas->removeBufferForDisplay (0);
    value->setValueAsString ("125");
    EXPECT_EQ (value->getCurrentValueAsString(), "-125");
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -125.0f);

    XmlElement savedRoot ("ROOT");
    options->saveParameters (&savedRoot);
    XmlElement offRoot (savedRoot);
    auto* offPane =
        offRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (offPane, nullptr);
    offPane->setAttribute ("spikeRaster", "OFF");
    LfpThreadTrackingComboBoxListener listener;
    spikeRaster->addListener (&listener);
    options->loadParameters (&offRoot);
    options->loadParameters (&savedRoot);
    spikeRaster->removeListener (&listener);
    EXPECT_EQ (listener.callbackCount.load(), 0);
    EXPECT_EQ (value->getCurrentValueAsString(), "-125");
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -125.0f);

    XmlElement defaultRoot (savedRoot);
    auto* defaultPane =
        defaultRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (defaultPane, nullptr);
    defaultPane->removeAttribute ("spikeRaster");
    spikeRaster->addListener (&listener);
    options->loadParameters (&defaultRoot);
    spikeRaster->removeListener (&listener);
    EXPECT_EQ (listener.callbackCount.load(), 0);
    EXPECT_EQ (value->getCurrentValueAsString(), "OFF");
    EXPECT_FALSE (display->getSpikeRasterPlotting());
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerWritesAndSelectsSpikeRaster)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    const auto id = prefix + "1.spike_raster";
    auto* spikeRaster =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    auto* display =
        findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    auto* subtractOffset =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix + "1.subtract_offset"));
    ASSERT_NE (spikeRaster, nullptr);
    ASSERT_NE (display, nullptr);
    ASSERT_NE (subtractOffset, nullptr);
    const auto window =
        static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto runAction =
        [&] (StringRef targetId,
             LfpWindowsUiaAction action,
             StringRef value = {})
    {
        LfpWindowsUiaInvokeResult actionResult;
        std::atomic<bool> workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (targetId)
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (value)
                                .toWideCharPointer()));
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (worker, workerReturned);
        return actionResult;
    };

    const auto description =
        L"Choose the negative-going spike raster threshold for LFP display 1: OFF shows continuous traces; -50, -100, -150, -200, -300, -400, -500, or a custom non-zero magnitude up to 500 replaces continuous traces with raster marks where the signal crosses the threshold relative to its per-pixel mean. Spike raster keeps subtract offset on. This changes display rendering only; acquisition and recording are unaffected.";
    const auto initialResult =
        runAction (id, LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (initialResult.invokeResult, S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 spike raster threshold");
    EXPECT_EQ (initialResult.help, description);
    EXPECT_TRUE (initialResult.valuePatternAvailable);
    EXPECT_EQ (initialResult.valueReadOnly, FALSE);
    EXPECT_TRUE (initialResult.expandCollapsePatternAvailable);
    EXPECT_TRUE (initialResult.invokePatternAvailable);
    EXPECT_EQ (initialResult.value, L"OFF");
    EXPECT_FALSE (spikeRaster->isPopupActive());

    const auto expandResult =
        runAction (id, LfpWindowsUiaAction::expand);
    EXPECT_EQ (expandResult.invokeResult, S_OK);
    EXPECT_EQ (expandResult.focusResult, S_OK);
    EXPECT_TRUE (spikeRaster->isPopupActive());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    const auto choiceId =
        id + ".choice.minus_100";
    const auto selectResult =
        runAction (
            choiceId,
            LfpWindowsUiaAction::select);
    EXPECT_EQ (selectResult.invokeResult, S_OK);
    EXPECT_EQ (spikeRaster->getText(), "-100");
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -100.0f);

    const auto setValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "125");
    EXPECT_EQ (setValueResult.invokeResult, S_OK);
    EXPECT_EQ (setValueResult.value, L"-125");
    EXPECT_EQ (spikeRaster->getText(), "-125");
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -125.0f);
    const auto offBeforePresetResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "OFF");
    EXPECT_EQ (offBeforePresetResult.invokeResult, S_OK);
    EXPECT_EQ (spikeRaster->getText(), "OFF");
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_FALSE (subtractOffset->getToggleState());
    const auto presetValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "-150");
    EXPECT_EQ (presetValueResult.invokeResult, S_OK);
    EXPECT_EQ (presetValueResult.value, L"-150");
    EXPECT_EQ (spikeRaster->getText(), "-150");
    EXPECT_TRUE (display->getSpikeRasterPlotting());
    EXPECT_FLOAT_EQ (display->getSpikeRasterThreshold(), -150.0f);
    EXPECT_TRUE (subtractOffset->getToggleState());
    const auto offAfterPresetResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "OFF");
    EXPECT_EQ (offAfterPresetResult.invokeResult, S_OK);
    EXPECT_EQ (spikeRaster->getText(), "OFF");
    EXPECT_FALSE (display->getSpikeRasterPlotting());
    EXPECT_FALSE (subtractOffset->getToggleState());
    const auto reexpandResult =
        runAction (id, LfpWindowsUiaAction::expand);
    EXPECT_EQ (reexpandResult.invokeResult, S_OK);
    EXPECT_EQ (reexpandResult.focusResult, S_OK);
    EXPECT_TRUE (spikeRaster->isPopupActive());

    spikeRaster->setEnabled (false);
    const auto disabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "-50");
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (UIA_E_ELEMENTNOTENABLED));
    spikeRaster->setEnabled (true);

    const auto hiddenPaneResult =
        runAction (
            prefix + "2.spike_raster",
            LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (hiddenPaneResult.invokeResult, E_FAIL);
    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult =
        runAction (id, LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (closedDrawerResult.invokeResult, E_FAIL);
}
#endif

TEST_F (LfpDisplayNodeTests,
        ExposesClipWarningForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const std::array<String, 2> choices {
        "OFF",
        "ON"
    };
    const std::array<String, 2> choiceIds {
        "off",
        "on"
    };
    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".clip_warning";
        auto* clipWarning =
            dynamic_cast<MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (clipWarning, nullptr)
            << id;
        EXPECT_EQ (
            clipWarning->getName(),
            "Clip Warning");
        EXPECT_EQ (
            clipWarning->getNumItems(),
            static_cast<int> (choices.size()));
        for (int choiceIndex = 0;
             choiceIndex
             < static_cast<int> (choices.size());
             ++choiceIndex)
        {
            EXPECT_EQ (
                clipWarning->getItemText (
                    choiceIndex),
                choices[choiceIndex]);
        }

        int choiceIndex = 0;
        for (PopupMenu::MenuItemIterator iterator (
                 *clipWarning->getRootMenu(),
                 false);
             iterator.next();)
        {
            ASSERT_LT (
                choiceIndex,
                static_cast<int> (
                    choiceIds.size()));
            EXPECT_EQ (
                iterator.getItem().accessibilityId,
                id
                    + ".choice."
                    + choiceIds[choiceIndex]);
            ++choiceIndex;
        }
        EXPECT_EQ (
            choiceIndex,
            static_cast<int> (choiceIds.size()));

        auto* handler =
            clipWarning->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (displayNumber)
                + " clip warning");
        const auto description =
            "Choose whether LFP display "
            + String (displayNumber)
            + " marks samples clipped by the current display range: OFF hides clip markers; ON draws a white marker at the clipped edge. This affects display rendering only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        auto* value =
            handler->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_FALSE (value->isReadOnly());
        EXPECT_EQ (
            value->getCurrentValueAsString(),
            "OFF");
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::press));
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::showMenu));
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::expand));
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::collapse));
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::LfpDisplayOptions> (
                *clipWarning)
                ->isVisible(),
            displayIndex == 0);
    }

    std::vector<Label*> labels;
    collectLfpDescendants (*canvas, labels);
    EXPECT_EQ (
        std::count_if (
            labels.begin(),
            labels.end(),
            [] (const Label* label)
            {
                return label->getName()
                       == "ClipWarningLabel";
            }),
        3);
    for (const auto* label : labels)
    {
        if (label->getName()
            == "ClipWarningLabel")
        {
            EXPECT_FALSE (label->isAccessible());
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        ClipWarningWorkerValueTracksRenderingAndSurvivesTeardown)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.clip_warning";
    auto* clipWarning =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    ASSERT_NE (clipWarning, nullptr);
    auto* split =
        findLfpDescendant<
            LfpViewer::LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (split, nullptr);
    auto liveHandler =
        clipWarning->createAccessibilityHandler();
    ASSERT_NE (liveHandler, nullptr);
    auto* value =
        liveHandler->getValueInterface();
    ASSERT_NE (value, nullptr);

    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    LfpThreadTrackingComboBoxListener listener;
    clipWarning->addListener (&listener);
    std::atomic<bool> workerReturned { false };
    std::thread worker (
        [&]
        {
            value->setValueAsString ("ON");
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (worker, workerReturned);
    clipWarning->removeListener (&listener);

    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (
        listener.callbackUsedMessageThread.load());
    EXPECT_EQ (clipWarning->getSelectedId(), 2);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "ON");
    EXPECT_TRUE (split->drawClipWarning);

    value->setValueAsString ("OFF");
    EXPECT_EQ (clipWarning->getSelectedId(), 1);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawClipWarning);

    value->setValueAsString ("unsupported");
    EXPECT_EQ (clipWarning->getSelectedId(), 1);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawClipWarning);

    clipWarning->setEnabled (false);
    value->setValueAsString ("ON");
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_EQ (clipWarning->getSelectedId(), 1);
    EXPECT_FALSE (split->drawClipWarning);

    clipWarning->setEnabled (true);
    clipWarning->synchroniseAccessibilityState();
    auto retainedHandler =
        clipWarning->createAccessibilityHandler();
    ASSERT_NE (retainedHandler, nullptr);
    auto* retainedValue =
        retainedHandler->getValueInterface();
    ASSERT_NE (retainedValue, nullptr);
    const auto retainedActions =
        retainedHandler->getActions();
    EXPECT_EQ (
        retainedValue->getCurrentValueAsString(),
        "OFF");

    canvas.reset();
    workerReturned.store (false);
    bool stalePressFound = false;
    bool staleShowMenuFound = false;
    std::thread staleWorker (
        [&]
        {
            retainedValue->setValueAsString (
                "ON");
            stalePressFound =
                retainedActions.invoke (
                    AccessibilityActionType::press);
            staleShowMenuFound =
                retainedActions.invoke (
                    AccessibilityActionType::showMenu);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
    EXPECT_EQ (
        retainedValue->getCurrentValueAsString(),
        "OFF");
    EXPECT_TRUE (stalePressFound);
    EXPECT_TRUE (staleShowMenuFound);
}

TEST_F (LfpDisplayNodeTests,
        ClipWarningValueSurvivesZeroChannelsBufferRemovalAndXmlRestore)
{
    auto zeroChannelTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (zeroChannelProcessor->getNodeId())
        + ".lfp.display_1.clip_warning";
    auto* clipWarning =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    ASSERT_NE (clipWarning, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::LfpDisplayOptions> (
            *clipWarning);
    auto* split =
        findLfpDescendant<
            LfpViewer::LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (options, nullptr);
    ASSERT_NE (split, nullptr);
    auto* value =
        clipWarning
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    value->setValueAsString ("ON");
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "ON");
    EXPECT_TRUE (split->drawClipWarning);
    canvas->removeBufferForDisplay (0);
    value->setValueAsString ("OFF");
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawClipWarning);

    value->setValueAsString ("ON");
    XmlElement onRoot ("ROOT");
    options->saveParameters (&onRoot);
    auto* onPane =
        onRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (onPane, nullptr);
    EXPECT_EQ (
        onPane->getIntAttribute (
            "clipWarning"),
        2);

    value->setValueAsString ("OFF");
    LfpThreadTrackingComboBoxListener listener;
    clipWarning->addListener (&listener);
    options->loadParameters (&onRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "ON");
    EXPECT_TRUE (split->drawClipWarning);

    XmlElement offRoot (onRoot);
    auto* offPane =
        offRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (offPane, nullptr);
    offPane->setAttribute ("clipWarning", 1);
    options->loadParameters (&offRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawClipWarning);

    XmlElement missingRoot (onRoot);
    auto* missingPane =
        missingRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (missingPane, nullptr);
    missingPane->removeAttribute ("clipWarning");
    options->loadParameters (&missingRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawClipWarning);

    XmlElement malformedRoot (onRoot);
    auto* malformedPane =
        malformedRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (malformedPane, nullptr);
    malformedPane->setAttribute (
        "clipWarning",
        999);
    options->loadParameters (&malformedRoot);
    clipWarning->removeListener (&listener);
    EXPECT_EQ (listener.callbackCount.load(), 0);
    EXPECT_EQ (clipWarning->getSelectedId(), 1);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawClipWarning);
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaWritesAndSelectsClipWarning)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix + "1.clip_warning";
    auto* clipWarning =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    ASSERT_NE (clipWarning, nullptr);
    auto* split =
        findLfpDescendant<
            LfpViewer::LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (split, nullptr);
    const auto window =
        static_cast<HWND> (
            canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto runAction =
        [&] (StringRef targetId,
             LfpWindowsUiaAction action,
             StringRef newValue = {})
    {
        LfpWindowsUiaInvokeResult result;
        std::atomic<bool> workerReturned { false };
        std::thread worker (
            [&]
            {
                result =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (targetId)
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (newValue)
                                .toWideCharPointer()));
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return result;
    };

    const auto description =
        L"Choose whether LFP display 1 marks samples clipped by the current display range: OFF hides clip markers; ON draws a white marker at the clipped edge. This affects display rendering only; acquisition and recording are unaffected.";
    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 clip warning");
    EXPECT_EQ (
        initialResult.help,
        description);
    EXPECT_TRUE (
        initialResult.valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        FALSE);
    EXPECT_TRUE (
        initialResult
            .expandCollapsePatternAvailable);
    EXPECT_TRUE (
        initialResult.invokePatternAvailable);
    EXPECT_EQ (initialResult.value, L"OFF");

    const auto setValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "ON");
    EXPECT_EQ (
        setValueResult.invokeResult,
        S_OK);
    EXPECT_EQ (setValueResult.value, L"ON");
    EXPECT_EQ (clipWarning->getSelectedId(), 2);
    EXPECT_TRUE (split->drawClipWarning);

    const auto offResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "OFF");
    EXPECT_EQ (offResult.invokeResult, S_OK);
    EXPECT_EQ (offResult.value, L"OFF");
    EXPECT_FALSE (split->drawClipWarning);

    const auto expandResult =
        runAction (
            id,
            LfpWindowsUiaAction::expand);
    EXPECT_EQ (
        expandResult.focusResult,
        S_OK);
    EXPECT_EQ (
        expandResult.invokeResult,
        S_OK);
    EXPECT_TRUE (clipWarning->isPopupActive());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    const auto selectResult =
        runAction (
            id + ".choice.on",
            LfpWindowsUiaAction::select);
    EXPECT_EQ (
        selectResult.invokeResult,
        S_OK);
    EXPECT_EQ (clipWarning->getSelectedId(), 2);
    EXPECT_TRUE (split->drawClipWarning);

    clipWarning->setEnabled (false);
    const auto disabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "OFF");
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    clipWarning->setEnabled (true);

    const auto hiddenPaneResult =
        runAction (
            prefix + "2.clip_warning",
            LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);
    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult =
        runAction (
            id,
            LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (
        closedDrawerResult.invokeResult,
        E_FAIL);
}
#endif

TEST_F (LfpDisplayNodeTests,
        ExposesSaturationWarningForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const std::array<String, 6> choices {
        "OFF",
        "0.5",
        "100",
        "1000",
        "5000",
        "6389"
    };
    const std::array<String, 6> choiceIds {
        "off",
        "0_5",
        "100",
        "1000",
        "5000",
        "6389"
    };
    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";

    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        const auto displayNumber =
            displayIndex + 1;
        const auto id =
            prefix
            + String (displayNumber)
            + ".saturation_warning";
        auto* saturationWarning =
            dynamic_cast<MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    id));
        ASSERT_NE (saturationWarning, nullptr)
            << id;
        EXPECT_EQ (
            saturationWarning->getName(),
            "Saturation Warning");
        EXPECT_EQ (
            saturationWarning->getNumItems(),
            static_cast<int> (choices.size()));
        for (int choiceIndex = 0;
             choiceIndex
             < static_cast<int> (choices.size());
             ++choiceIndex)
        {
            EXPECT_EQ (
                saturationWarning->getItemText (
                    choiceIndex),
                choices[choiceIndex]);
        }

        int choiceIndex = 0;
        for (PopupMenu::MenuItemIterator iterator (
                 *saturationWarning->getRootMenu(),
                 false);
             iterator.next();)
        {
            ASSERT_LT (
                choiceIndex,
                static_cast<int> (
                    choiceIds.size()));
            EXPECT_EQ (
                iterator.getItem().accessibilityId,
                id
                    + ".choice."
                    + choiceIds[choiceIndex]);
            ++choiceIndex;
        }
        EXPECT_EQ (
            choiceIndex,
            static_cast<int> (choiceIds.size()));

        auto* handler =
            saturationWarning
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::comboBox);
        EXPECT_EQ (
            handler->getTitle(),
            "LFP display "
                + String (displayNumber)
                + " saturation warning threshold");
        const auto description =
            "Choose the absolute raw sample-magnitude threshold for saturation warnings in LFP display "
            + String (displayNumber)
            + ": OFF hides saturation warnings; 0.5, 100, 1000, 5000, or 6389 shows a red-and-white warning when a raw sample exceeds the selected magnitude. This affects display rendering only; acquisition and recording are unaffected.";
        EXPECT_EQ (
            handler->getDescription(),
            description);
        EXPECT_EQ (
            handler->getHelp(),
            description);
        auto* value =
            handler->getValueInterface();
        ASSERT_NE (value, nullptr);
        EXPECT_FALSE (value->isReadOnly());
        EXPECT_EQ (
            value->getCurrentValueAsString(),
            "OFF");
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::press));
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::showMenu));
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::expand));
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::collapse));
        EXPECT_EQ (
            findLfpAncestor<
                LfpViewer::LfpDisplayOptions> (
                *saturationWarning)
                ->isVisible(),
            displayIndex == 0);
    }

    std::vector<Label*> labels;
    collectLfpDescendants (*canvas, labels);
    EXPECT_EQ (
        std::count_if (
            labels.begin(),
            labels.end(),
            [] (const Label* label)
            {
                return label->getName()
                       == "SaturationWarningLabel";
            }),
        3);
    for (const auto* label : labels)
    {
        if (label->getName()
            == "SaturationWarningLabel")
        {
            EXPECT_FALSE (label->isAccessible());
        }
    }
}

TEST_F (LfpDisplayNodeTests,
        ExcludesLegacyUnlaidOutOverlapFromAccessibilityForEveryPane)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto splitters =
        getDisplaySplitters (*canvas);
    ASSERT_EQ (splitters.size(), 3);

    for (const auto* splitter :
         splitters)
    {
        std::vector<ComboBox*> comboBoxes;
        collectLfpDescendants (
            *splitter->options,
            comboBoxes);
        const auto overlap =
            std::find_if (
                comboBoxes.begin(),
                comboBoxes.end(),
                [] (const ComboBox* comboBox)
                {
                    return comboBox->getName()
                           == "Overlap";
                });

        ASSERT_NE (overlap, comboBoxes.end());
        EXPECT_TRUE (
            (*overlap)->getBounds().isEmpty());
        EXPECT_FALSE (
            (*overlap)->isAccessible());
        EXPECT_EQ (
            (*overlap)
                ->getAccessibilityHandler(),
            nullptr);
        EXPECT_FLOAT_EQ (
            splitter->channelOverlapFactor,
            2.0f);
    }
}

TEST_F (LfpDisplayNodeTests,
        SaturationWarningWorkerValueTracksRenderingAndSurvivesTeardown)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.saturation_warning";
    auto* saturationWarning =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    ASSERT_NE (saturationWarning, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::LfpDisplayOptions> (
            *saturationWarning);
    auto* split =
        findLfpDescendant<
            LfpViewer::LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (options, nullptr);
    ASSERT_NE (split, nullptr);
    auto liveHandler =
        saturationWarning
            ->createAccessibilityHandler();
    ASSERT_NE (liveHandler, nullptr);
    auto* value =
        liveHandler->getValueInterface();
    ASSERT_NE (value, nullptr);

    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    LfpThreadTrackingComboBoxListener listener;
    saturationWarning->addListener (&listener);
    std::atomic<bool> workerReturned { false };
    std::thread worker (
        [&]
        {
            value->setValueAsString ("0.5");
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (worker, workerReturned);
    saturationWarning->removeListener (&listener);

    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (
        listener.callbackUsedMessageThread.load());
    EXPECT_EQ (
        saturationWarning->getSelectedId(),
        2);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "0.5");
    EXPECT_TRUE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.5f);

    const std::array<std::pair<String, float>, 4>
        remainingChoices {
            std::pair<String, float> {
                "100",
                100.0f },
            std::pair<String, float> {
                "1000",
                1000.0f },
            std::pair<String, float> {
                "5000",
                5000.0f },
            std::pair<String, float> {
                "6389",
                6389.0f }
        };
    for (const auto& [text, threshold] :
         remainingChoices)
    {
        value->setValueAsString (text);
        EXPECT_EQ (
            value->getCurrentValueAsString(),
            text);
        EXPECT_TRUE (
            split->drawSaturationWarning);
        EXPECT_FLOAT_EQ (
            options
                ->selectedSaturationValueFloat,
            threshold);
    }

    value->setValueAsString ("OFF");
    EXPECT_EQ (
        saturationWarning->getSelectedId(),
        1);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.0f);

    value->setValueAsString ("unsupported");
    EXPECT_EQ (
        saturationWarning->getSelectedId(),
        1);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.0f);

    saturationWarning->setEnabled (false);
    value->setValueAsString ("6389");
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_EQ (
        saturationWarning->getSelectedId(),
        1);
    EXPECT_FALSE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.0f);

    saturationWarning->setEnabled (true);
    saturationWarning
        ->synchroniseAccessibilityState();
    auto retainedHandler =
        saturationWarning
            ->createAccessibilityHandler();
    ASSERT_NE (retainedHandler, nullptr);
    auto* retainedValue =
        retainedHandler->getValueInterface();
    ASSERT_NE (retainedValue, nullptr);
    const auto retainedActions =
        retainedHandler->getActions();
    EXPECT_EQ (
        retainedValue->getCurrentValueAsString(),
        "OFF");

    canvas.reset();
    workerReturned.store (false);
    bool stalePressFound = false;
    bool staleShowMenuFound = false;
    std::thread staleWorker (
        [&]
        {
            retainedValue->setValueAsString (
                "100");
            stalePressFound =
                retainedActions.invoke (
                    AccessibilityActionType::press);
            staleShowMenuFound =
                retainedActions.invoke (
                    AccessibilityActionType::showMenu);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
    EXPECT_EQ (
        retainedValue->getCurrentValueAsString(),
        "OFF");
    EXPECT_TRUE (stalePressFound);
    EXPECT_TRUE (staleShowMenuFound);
}

TEST_F (LfpDisplayNodeTests,
        SaturationWarningValueSurvivesZeroChannelsBufferRemovalAndXmlRestore)
{
    auto zeroChannelTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (
            zeroChannelProcessor
                ->getNodeId())
        + ".lfp.display_1.saturation_warning";
    auto* saturationWarning =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    ASSERT_NE (saturationWarning, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::LfpDisplayOptions> (
            *saturationWarning);
    auto* split =
        findLfpDescendant<
            LfpViewer::LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (options, nullptr);
    ASSERT_NE (split, nullptr);
    auto* value =
        saturationWarning
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    value->setValueAsString ("100");
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "100");
    EXPECT_EQ (
        saturationWarning->getSelectedId(),
        3);
    EXPECT_TRUE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        100.0f);

    canvas->removeBufferForDisplay (0);
    value->setValueAsString ("OFF");
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.0f);

    value->setValueAsString ("6389");
    XmlElement onRoot ("ROOT");
    options->saveParameters (&onRoot);
    auto* onPane =
        onRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (onPane, nullptr);
    EXPECT_EQ (
        onPane->getIntAttribute (
            "satWarning"),
        6);

    value->setValueAsString ("OFF");
    LfpThreadTrackingComboBoxListener listener;
    saturationWarning->addListener (&listener);
    options->loadParameters (&onRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "6389");
    EXPECT_TRUE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        6389.0f);

    XmlElement offRoot (onRoot);
    auto* offPane =
        offRoot.getChildByName ("LFPDISPLAY0");
    ASSERT_NE (offPane, nullptr);
    offPane->setAttribute ("satWarning", 1);
    options->loadParameters (&offRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.0f);

    XmlElement missingRoot (onRoot);
    auto* missingPane =
        missingRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (missingPane, nullptr);
    missingPane->removeAttribute ("satWarning");
    options->loadParameters (&missingRoot);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.0f);

    XmlElement malformedRoot (onRoot);
    auto* malformedPane =
        malformedRoot.getChildByName (
            "LFPDISPLAY0");
    ASSERT_NE (malformedPane, nullptr);
    malformedPane->setAttribute (
        "satWarning",
        999);
    options->loadParameters (&malformedRoot);
    saturationWarning->removeListener (
        &listener);
    EXPECT_EQ (listener.callbackCount.load(), 0);
    EXPECT_EQ (
        saturationWarning->getSelectedId(),
        1);
    EXPECT_EQ (
        value->getCurrentValueAsString(),
        "OFF");
    EXPECT_FALSE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.0f);
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaWritesAndSelectsSaturationWarning)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.saturation_warning";
    auto* saturationWarning =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    ASSERT_NE (saturationWarning, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::LfpDisplayOptions> (
            *saturationWarning);
    auto* split =
        findLfpDescendant<
            LfpViewer::LfpDisplaySplitter> (
            *canvas);
    ASSERT_NE (options, nullptr);
    ASSERT_NE (split, nullptr);
    const auto window =
        static_cast<HWND> (
            canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto runAction =
        [&] (StringRef targetId,
             LfpWindowsUiaAction action,
             StringRef newValue = {})
    {
        LfpWindowsUiaInvokeResult result;
        std::atomic<bool> workerReturned { false };
        std::thread worker (
            [&]
            {
                result =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (targetId)
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (newValue)
                                .toWideCharPointer()));
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return result;
    };

    const auto description =
        L"Choose the absolute raw sample-magnitude threshold for saturation warnings in LFP display 1: OFF hides saturation warnings; 0.5, 100, 1000, 5000, or 6389 shows a red-and-white warning when a raw sample exceeds the selected magnitude. This affects display rendering only; acquisition and recording are unaffected.";
    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 saturation warning threshold");
    EXPECT_EQ (
        initialResult.help,
        description);
    EXPECT_TRUE (
        initialResult.valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        FALSE);
    EXPECT_TRUE (
        initialResult
            .expandCollapsePatternAvailable);
    EXPECT_TRUE (
        initialResult.invokePatternAvailable);
    EXPECT_EQ (initialResult.value, L"OFF");

    const auto setValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "1000");
    EXPECT_EQ (
        setValueResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        setValueResult.value,
        L"1000");
    EXPECT_EQ (
        saturationWarning->getSelectedId(),
        4);
    EXPECT_TRUE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        1000.0f);

    const auto expandResult =
        runAction (
            id,
            LfpWindowsUiaAction::expand);
    EXPECT_EQ (
        expandResult.focusResult,
        S_OK);
    EXPECT_EQ (
        expandResult.invokeResult,
        S_OK);
    EXPECT_TRUE (
        saturationWarning->isPopupActive());
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    const auto selectResult =
        runAction (
            id + ".choice.0_5",
            LfpWindowsUiaAction::select);
    EXPECT_EQ (
        selectResult.invokeResult,
        S_OK);
    MessageManager::getInstance()
        ->runDispatchLoopUntil (30);
    EXPECT_EQ (
        saturationWarning->getSelectedId(),
        2);
    EXPECT_TRUE (split->drawSaturationWarning);
    EXPECT_FLOAT_EQ (
        options->selectedSaturationValueFloat,
        0.5f);

    saturationWarning->setEnabled (false);
    const auto disabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::setValue,
            "OFF");
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    saturationWarning->setEnabled (true);

    const auto hiddenPaneResult =
        runAction (
            prefix
                + "2.saturation_warning",
            LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);
    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult =
        runAction (
            id,
            LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (
        closedDrawerResult.invokeResult,
        E_FAIL);
}
#endif

TEST_F (LfpDisplayNodeTests,
        ColourGroupingWorkerSelectsByShankOnMessageThread)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.colour_grouping";
    auto* grouping =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        grouping,
        nullptr);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        display,
        nullptr);
    auto* handler =
        grouping
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    auto* value =
        handler
            ->getValueInterface();
    ASSERT_NE (
        value,
        nullptr);

    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            30);
    LfpThreadTrackingComboBoxListener
        listener;
    grouping->addListener (
        &listener);
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            value->setValueAsString (
                "By Shank");
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || grouping->getSelectedId()
                        != 6);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
    grouping->removeListener (
        &listener);

    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        grouping->getText(),
        "By Shank");
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");

    value->setValueAsString (
        "unsupported");
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
}

TEST_F (LfpDisplayNodeTests,
        ChannelSkipWorkerSelectionTracksModelAndProgrammaticState)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.channel_skip";
    auto* channelSkip =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (channelSkip, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *channelSkip);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (options, nullptr);
    ASSERT_NE (display, nullptr);
    auto* value =
        channelSkip
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    LfpThreadTrackingComboBoxListener listener;
    channelSkip->addListener (&listener);
    std::atomic<bool> workerReturned { false };
    std::thread worker (
        [&]
        {
            value->setValueAsString ("64");
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || channelSkip->getSelectedId() != 7);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (worker, workerReturned);
    channelSkip->removeListener (&listener);

    EXPECT_EQ (listener.callbackCount.load(), 1);
    EXPECT_TRUE (listener.callbackUsedMessageThread.load());
    EXPECT_EQ (channelSkip->getSelectedId(), 7);
    EXPECT_EQ (value->getCurrentValueAsString(), "64");
    EXPECT_EQ (display->getChannelDisplaySkipAmount(), 64);

    options->setChannelDisplaySkipSelection (3);
    EXPECT_EQ (channelSkip->getSelectedId(), 3);
    EXPECT_EQ (value->getCurrentValueAsString(), "4");
    EXPECT_EQ (display->getChannelDisplaySkipAmount(), 4);

    channelSkip->setEnabled (false);
    value->setValueAsString ("8");
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    EXPECT_EQ (channelSkip->getSelectedId(), 3);
    EXPECT_EQ (display->getChannelDisplaySkipAmount(), 4);

    const auto retainedActions =
        channelSkip
            ->getAccessibilityHandler()
            ->getActions();
    canvas.reset();
    workerReturned.store (false);
    bool staleActionFound = false;
    std::thread staleWorker (
        [&]
        {
            staleActionFound =
                retainedActions.invoke (
                    AccessibilityActionType::press);
            workerReturned.store (true);
        });
    for (int attempt = 0;
         attempt < 100 && ! workerReturned.load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (10);
    }
    joinLfpWorkerOrAbort (staleWorker, workerReturned);
    EXPECT_TRUE (staleActionFound);
}

TEST_F (LfpDisplayNodeTests,
        ChannelSkipSelectionIdsMapToModelFactorsAndAccessibilityValues)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_1.channel_skip";
    auto* channelSkip =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    auto* display =
        findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (channelSkip, nullptr);
    ASSERT_NE (display, nullptr);
    auto* options =
        findLfpAncestor<LfpViewer::LfpDisplayOptions> (
            *channelSkip);
    ASSERT_NE (options, nullptr);
    auto* value =
        channelSkip
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    struct ChannelSkipCase
    {
        int selectedId;
        int factor;
        String value;
    };
    const std::array<ChannelSkipCase, 7> cases {
        ChannelSkipCase { 1, 1, "None" },
        ChannelSkipCase { 2, 2, "2" },
        ChannelSkipCase { 3, 4, "4" },
        ChannelSkipCase { 4, 8, "8" },
        ChannelSkipCase { 5, 16, "16" },
        ChannelSkipCase { 6, 32, "32" },
        ChannelSkipCase { 7, 64, "64" }
    };
    for (const auto& testCase : cases)
    {
        options->setChannelDisplaySkipSelection (
            testCase.selectedId);
        EXPECT_EQ (
            channelSkip->getSelectedId(),
            testCase.selectedId);
        EXPECT_EQ (
            value->getCurrentValueAsString(),
            testCase.value);
        EXPECT_EQ (
            display->getChannelDisplaySkipAmount(),
            testCase.factor);
    }
}

TEST_F (LfpDisplayNodeTests,
        ChannelSkipUsesEligibleChannelIndexBeforeSortAndReverse)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    auto* display =
        findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    auto* splitter =
        findLfpDescendant<LfpViewer::LfpDisplaySplitter> (*canvas);
    ASSERT_NE (display, nullptr);
    ASSERT_NE (splitter, nullptr);
    ASSERT_NE (splitter->displayBuffer, nullptr);
    ASSERT_EQ (display->channelInfo.size(), 16);
    ASSERT_EQ (splitter->displayBuffer->channelMetadata.size(), 16);

    const auto getPhysicalOrder =
        [&]
    {
        std::vector<int> order;
        for (const auto& track : display->drawableChannels)
        {
            int physicalChannel = -1;
            for (int channel = 0;
                 channel < display->channelInfo.size();
                 ++channel)
            {
                if (display->channelInfo[channel] == track.channelInfo)
                {
                    physicalChannel = channel;
                    break;
                }
            }
            EXPECT_NE (physicalChannel, -1);
            order.push_back (physicalChannel);
        }
        return order;
    };

    Array<int> eligibleChannels;
    for (const auto physicalChannel : { 1, 3, 5, 7, 9 })
        eligibleChannels.add (physicalChannel);
    for (int channel = 0;
         channel < splitter->displayBuffer->channelMetadata.size();
         ++channel)
    {
        auto& metadata =
            splitter->displayBuffer->channelMetadata.getReference (channel);
        metadata.description = String (channel);
        metadata.group = 2;
        metadata.ypos = float (channel);
        metadata.xpos = 0.0f;
        metadata.hasGroupMetadata = false;
        metadata.hasYposMetadata = false;
        metadata.hasXposMetadata = false;
    }
    splitter->setFilteredChannels (eligibleChannels);
    display->options->setChannelsReversed (false);
    display->options->setSortByDepth (false);
    display->options->setChannelDisplaySkipSelection (1);
    EXPECT_EQ (
        getPhysicalOrder(),
        (std::vector<int> { 1, 3, 5, 7, 9 }));

    display->options->setChannelDisplaySkipSelection (2);
    EXPECT_EQ (
        getPhysicalOrder(),
        (std::vector<int> { 1, 5, 9 }));

    auto& channelOne =
        splitter->displayBuffer->channelMetadata.getReference (1);
    channelOne.group = 2;
    channelOne.ypos = 100.0f;
    channelOne.hasGroupMetadata = true;
    channelOne.hasYposMetadata = true;
    channelOne.hasXposMetadata = true;
    auto& channelFive =
        splitter->displayBuffer->channelMetadata.getReference (5);
    channelFive.group = 1;
    channelFive.ypos = 100.0f;
    channelFive.hasGroupMetadata = true;
    channelFive.hasYposMetadata = true;
    channelFive.hasXposMetadata = true;
    auto& channelNine =
        splitter->displayBuffer->channelMetadata.getReference (9);
    channelNine.group = 0;
    channelNine.ypos = 100.0f;
    channelNine.hasGroupMetadata = true;
    channelNine.hasYposMetadata = true;
    channelNine.hasXposMetadata = true;

    canvas->updateSettings();
    display->options->setSortByDepth (true);
    EXPECT_EQ (
        getPhysicalOrder(),
        (std::vector<int> { 9, 5, 1 }));
    display->options->setChannelsReversed (true);
    EXPECT_EQ (
        getPhysicalOrder(),
        (std::vector<int> { 1, 5, 9 }));
}

TEST_F (LfpDisplayNodeTests,
        ChannelSkipRemainsConsistentWithoutChannelsOrDisplayBuffer)
{
    auto zeroChannelTester =
        std::make_unique<ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto id =
        "oe.processor."
        + String (zeroChannelProcessor->getNodeId())
        + ".lfp.display_1.channel_skip";
    auto* channelSkip =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    auto* display =
        findLfpDescendant<
            LfpViewer::LfpDisplay> (*canvas);
    auto* splitter =
        findLfpDescendant<
            LfpViewer::
                LfpDisplaySplitter> (*canvas);
    ASSERT_NE (channelSkip, nullptr);
    ASSERT_NE (display, nullptr);
    ASSERT_NE (splitter, nullptr);
    ASSERT_EQ (zeroChannelProcessor->getNumInputs(), 0);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *channelSkip);
    ASSERT_NE (options, nullptr);
    auto* value =
        channelSkip
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (value, nullptr);

    value->setValueAsString ("32");
    EXPECT_EQ (channelSkip->getSelectedId(), 6);
    EXPECT_EQ (value->getCurrentValueAsString(), "32");
    EXPECT_EQ (display->getChannelDisplaySkipAmount(), 32);

    auto* displayBuffer = splitter->displayBuffer;
    splitter->displayBuffer = nullptr;
    options->setChannelDisplaySkipSelection (1);
    EXPECT_EQ (channelSkip->getSelectedId(), 1);
    EXPECT_EQ (value->getCurrentValueAsString(), "None");
    EXPECT_EQ (display->getChannelDisplaySkipAmount(), 1);
    splitter->displayBuffer = displayBuffer;
}

TEST (LfpChannelSkipTests,
      NormalisesMalformedPersistedIds)
{
    constexpr int numberOfChoices = 7;
    for (int validId = 1;
         validId <= numberOfChoices;
         ++validId)
    {
        EXPECT_EQ (
            LfpViewer::normaliseLfpChannelSkipId (
                validId,
                numberOfChoices),
            validId);
    }

    for (const auto invalidId : { -1, 0, 8, 999 })
    {
        EXPECT_EQ (
            LfpViewer::normaliseLfpChannelSkipId (
                invalidId,
                numberOfChoices),
            1);
    }
}

TEST_F (LfpDisplayNodeTests,
        ChannelSkipXmlRoundTripsEveryPaneAndNormalisesMalformedIds)
{
    auto canvas =
        std::make_unique<
            LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (900, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    std::array<MessageThreadComboBox*, 3> channelSkips {};
    std::array<LfpViewer::LfpDisplayOptions*, 3> options {};
    std::array<LfpViewer::LfpDisplay*, 3> displays {};
    std::array<AccessibilityValueInterface*, 3> values {};
    std::array<LfpThreadTrackingComboBoxListener, 3> listeners;
    const std::array<int, 3> selectedIds { 1, 4, 7 };
    const std::array<int, 3> skipAmounts { 1, 8, 64 };
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        channelSkips[displayIndex] =
            dynamic_cast<MessageThreadComboBox*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (displayIndex + 1)
                        + ".channel_skip"));
        ASSERT_NE (channelSkips[displayIndex], nullptr);
        options[displayIndex] =
            findLfpAncestor<LfpViewer::LfpDisplayOptions> (
                *channelSkips[displayIndex]);
        ASSERT_NE (options[displayIndex], nullptr);
        values[displayIndex] =
            channelSkips[displayIndex]
                ->getAccessibilityHandler()
                ->getValueInterface();
        ASSERT_NE (values[displayIndex], nullptr);
        channelSkips[displayIndex]
            ->addListener (&listeners[displayIndex]);
        options[displayIndex]
            ->setChannelDisplaySkipSelection (selectedIds[displayIndex]);
    }
    std::vector<LfpViewer::LfpDisplay*> allDisplays;
    collectLfpDescendants (*canvas, allDisplays);
    ASSERT_EQ (allDisplays.size(), 3);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        for (auto* candidate : allDisplays)
        {
            if (candidate->options == options[displayIndex])
            {
                displays[displayIndex] = candidate;
                break;
            }
        }
        ASSERT_NE (displays[displayIndex], nullptr);
    }

    XmlElement savedRoot ("ROOT");
    for (auto* paneOptions : options)
        paneOptions->saveParameters (&savedRoot);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        auto* savedPane =
            savedRoot.getChildByName (
                "LFPDISPLAY" + String (displayIndex));
        ASSERT_NE (savedPane, nullptr);
        EXPECT_EQ (
            savedPane->getIntAttribute ("channelSkip"),
            selectedIds[displayIndex]);
        options[displayIndex]
            ->setChannelDisplaySkipSelection (2);
    }

    for (auto* paneOptions : options)
        paneOptions->loadParameters (&savedRoot);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        EXPECT_EQ (
            channelSkips[displayIndex]->getSelectedId(),
            selectedIds[displayIndex]);
        EXPECT_EQ (
            displays[displayIndex]->getChannelDisplaySkipAmount(),
            skipAmounts[displayIndex]);
        EXPECT_EQ (
            listeners[displayIndex].callbackCount.load(),
            0);
    }

    for (auto* savedPane : savedRoot.getChildIterator())
        savedPane->setAttribute ("channelSkip", 999);
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        options[displayIndex]
            ->loadParameters (&savedRoot);
        EXPECT_EQ (channelSkips[displayIndex]->getSelectedId(), 1);
        EXPECT_EQ (
            values[displayIndex]->getCurrentValueAsString(),
            "None");
        EXPECT_EQ (
            displays[displayIndex]->getChannelDisplaySkipAmount(),
            1);
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
    }

    for (auto* savedPane : savedRoot.getChildIterator())
        savedPane->removeAttribute ("channelSkip");
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        options[displayIndex]
            ->setChannelDisplaySkipSelection (7);
        options[displayIndex]
            ->loadParameters (&savedRoot);
        EXPECT_EQ (channelSkips[displayIndex]->getSelectedId(), 1);
        EXPECT_EQ (
            displays[displayIndex]->getChannelDisplaySkipAmount(),
            1);
        EXPECT_EQ (listeners[displayIndex].callbackCount.load(), 0);
        channelSkips[displayIndex]
            ->removeListener (&listeners[displayIndex]);
    }
}

TEST_F (LfpDisplayNodeTests,
        ColourGroupingRemainsConsistentWithoutChannels)
{
    auto zeroChannelTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    0,
                    sampleRate,
                    bitVolts }));
    auto* zeroChannelProcessor =
        zeroChannelTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::
                    SINK);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            zeroChannelProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto id =
        "oe.processor."
        + String (
            zeroChannelProcessor
                ->getNodeId())
        + ".lfp.display_1.colour_grouping";
    auto* grouping =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        grouping,
        nullptr);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        display,
        nullptr);
    ASSERT_EQ (
        zeroChannelProcessor
            ->getNumInputs(),
        0);

    auto* value =
        grouping
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (
        value,
        nullptr);
    value->setValueAsString (
        "By Shank");

    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");
}

TEST (LfpColourGroupingTests,
      NormalisesMalformedPersistedIds)
{
    constexpr int numberOfChoices =
        6;
    for (int validId = 1;
         validId <= numberOfChoices;
         ++validId)
    {
        EXPECT_EQ (
            LfpViewer::
                normaliseLfpColourGroupingId (
                    validId,
                    numberOfChoices),
            validId);
    }

    for (const auto invalidId :
         { -1, 0, 7, 999 })
    {
        EXPECT_EQ (
            LfpViewer::
                normaliseLfpColourGroupingId (
                    invalidId,
                    numberOfChoices),
            1);
    }
}

TEST_F (LfpDisplayNodeTests,
        ColourGroupingXmlParameterRoundTripsAndNormalisesMalformedIds)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.colour_grouping";
    auto* grouping =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        grouping,
        nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *grouping);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        options,
        nullptr);
    ASSERT_NE (
        display,
        nullptr);
    auto* value =
        grouping
            ->getAccessibilityHandler()
            ->getValueInterface();
    ASSERT_NE (
        value,
        nullptr);

    LfpThreadTrackingComboBoxListener
        listener;
    grouping->addListener (
        &listener);
    options
        ->setColourGroupingSelection (
            6);
    XmlElement savedNode (
        "LFPDISPLAY0");
    options
        ->saveColourGroupingParameter (
            savedNode);
    EXPECT_TRUE (
        savedNode.hasAttribute (
            "colourGrouping"));
    EXPECT_EQ (
        savedNode.getIntAttribute (
            "colourGrouping"),
        6);

    options
        ->setColourGroupingSelection (
            1);
    EXPECT_EQ (
        grouping->getSelectedId(),
        1);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "1");
    options
        ->restoreColourGroupingParameter (
            savedNode);
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        grouping->getText(),
        "By Shank");
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "By Shank");
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);

    const std::array<String, 6>
        expectedValues {
            "1",
            "2",
            "4",
            "8",
            "16",
            "By Shank"
        };
    for (int itemId = 1;
         itemId <= 6;
         ++itemId)
    {
        XmlElement node (
            "LFPDISPLAY0");
        node.setAttribute (
            "colourGrouping",
            itemId);
        options
            ->restoreColourGroupingParameter (
                node);
        EXPECT_EQ (
            grouping->getSelectedId(),
            itemId);
        EXPECT_EQ (
            grouping->getText(),
            expectedValues[
                itemId - 1]);
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            expectedValues[
                itemId - 1]);
        EXPECT_EQ (
            display
                ->getColourGrouping(),
            expectedValues[
                itemId - 1]);
    }

    for (const auto invalidId :
         { -1, 0, 7, 999 })
    {
        XmlElement node (
            "LFPDISPLAY0");
        node.setAttribute (
            "colourGrouping",
            invalidId);
        options
            ->setColourGroupingSelection (
                6);
        options
            ->restoreColourGroupingParameter (
                node);
        EXPECT_EQ (
            grouping->getSelectedId(),
            1);
        EXPECT_EQ (
            grouping->getText(),
            "1");
        EXPECT_EQ (
            value
                ->getCurrentValueAsString(),
            "1");
        EXPECT_EQ (
            display
                ->getColourGrouping(),
            "1");
    }

    XmlElement missingAttributeNode (
        "LFPDISPLAY0");
    options
        ->setColourGroupingSelection (
            6);
    options
        ->restoreColourGroupingParameter (
            missingAttributeNode);
    EXPECT_EQ (
        grouping->getSelectedId(),
        1);
    EXPECT_EQ (
        value
            ->getCurrentValueAsString(),
        "1");
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "1");
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        0);
    grouping->removeListener (
        &listener);
}

TEST_F (LfpDisplayNodeTests,
        RangeTypeSelectionRunsOnMessageThreadAndTracksProgrammaticState)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    auto* data =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.data"));
    auto* aux =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.aux"));
    auto* adc =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.adc"));
    auto* range =
        findLfpDescendantById (
            *canvas,
            prefix
                + "voltage_range");
    ASSERT_NE (data, nullptr);
    ASSERT_NE (aux, nullptr);
    ASSERT_NE (adc, nullptr);
    ASSERT_NE (range, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *aux);
    ASSERT_NE (
        options,
        nullptr);
    auto* dataHandler =
        data
            ->getAccessibilityHandler();
    auto* auxHandler =
        aux
            ->getAccessibilityHandler();
    auto* adcHandler =
        adc
            ->getAccessibilityHandler();
    ASSERT_NE (
        dataHandler,
        nullptr);
    ASSERT_NE (
        auxHandler,
        nullptr);
    ASSERT_NE (
        adcHandler,
        nullptr);
    const auto auxActions =
        auxHandler->getActions();
    LfpThreadTrackingButtonListener
        listener;
    aux->addListener (
        &listener);

    std::atomic<bool>
        workerReturned { false };
    bool pressed = false;
    std::thread worker (
        [&]
        {
            pressed =
                auxActions.invoke (
                    AccessibilityActionType::
                        press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || options
                            ->getSelectedType()
                        != ContinuousChannel::
                               Type::AUX);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);
    aux->removeListener (
        &listener);

    EXPECT_TRUE (pressed);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_FALSE (
        dataHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        auxHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_FALSE (
        adcHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Auto");
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getHelp(),
        "Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");

    options->setSelectedType (
        ContinuousChannel::Type::
            ADC);
    EXPECT_FALSE (
        dataHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_FALSE (
        auxHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_TRUE (
        adcHandler
            ->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "10.0");
}

TEST_F (LfpDisplayNodeTests,
        RangeTypeActionsIgnoreDisabledAndDestroyedControls)
{
    AccessibilityActions
        retainedActions;
    std::atomic<bool>
        workerReturned { false };
    {
        auto canvas =
            std::make_unique<
                LfpViewer::
                    LfpDisplayCanvas> (
                processor,
                LfpViewer::
                    SplitLayouts::SINGLE,
                false);
        canvas->updateSettings();
        canvas->setSize (600, 800);
        canvas->addToDesktop (0);
        canvas->setVisible (true);

        const auto prefix =
            "oe.processor."
            + String (
                processor->getNodeId())
            + ".lfp.display_1.";
        auto* data =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + "channel_type.data"));
        auto* aux =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + "channel_type.aux"));
        ASSERT_NE (data, nullptr);
        ASSERT_NE (aux, nullptr);
        auto* options =
            findLfpAncestor<
                LfpViewer::
                    LfpDisplayOptions> (
                *aux);
        ASSERT_NE (
            options,
            nullptr);
        retainedActions =
            aux
                ->getAccessibilityHandler()
                ->getActions();

        aux->setEnabled (false);
        bool disabledActionFound =
            false;
        std::thread disabledWorker (
            [&]
            {
                disabledActionFound =
                    retainedActions
                        .invoke (
                            AccessibilityActionType::
                                press);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && ! workerReturned
                           .load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            disabledWorker,
            workerReturned);
        EXPECT_TRUE (
            disabledActionFound);
        EXPECT_TRUE (
            data->getToggleState());
        EXPECT_FALSE (
            aux->getToggleState());
        EXPECT_EQ (
            options
                ->getSelectedType(),
            ContinuousChannel::Type::
                ELECTRODE);
    }

    workerReturned.store (
        false);
    bool staleActionFound = false;
    std::thread staleWorker (
        [&]
        {
            staleActionFound =
                retainedActions
                    .invoke (
                        AccessibilityActionType::
                            press);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! workerReturned
                       .load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        staleWorker,
        workerReturned);
    EXPECT_TRUE (
        staleActionFound);
}

TEST_F (LfpDisplayNodeTests,
        PauseToggleRunsOnMessageThreadAndTracksProgrammaticState)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    auto* pause =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "pause"));
    auto* timebase =
        findLfpDescendantById (
            *canvas,
            prefix
                + "timebase");
    ASSERT_NE (
        pause,
        nullptr);
    ASSERT_NE (
        timebase,
        nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *pause);
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    auto* timescale =
        findLfpDescendant<
            LfpViewer::
                LfpTimescale> (
            *canvas);
    ASSERT_NE (
        options,
        nullptr);
    ASSERT_NE (
        display,
        nullptr);
    ASSERT_NE (
        timescale,
        nullptr);
    auto* handler =
        pause
            ->getAccessibilityHandler();
    ASSERT_NE (
        handler,
        nullptr);
    const auto actions =
        handler->getActions();
    LfpThreadTrackingButtonListener
        listener;
    pause->addListener (
        &listener);

    std::atomic<bool>
        workerReturned { false };
    bool toggled = false;
    std::thread worker (
        [&]
        {
            toggled =
                actions.invoke (
                    AccessibilityActionType::
                        toggle);
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
    pause->removeListener (
        &listener);

    EXPECT_TRUE (toggled);
    EXPECT_TRUE (
        workerReturned.load());
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_TRUE (
        pause->getToggleState());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Paused");
    EXPECT_FALSE (
        timebase->isEnabled());
    EXPECT_TRUE (
        display->isPaused());
    EXPECT_TRUE (
        timescale
            ->getPausedState());

    EXPECT_TRUE (
        actions.invoke (
            AccessibilityActionType::
                toggle));
    EXPECT_FALSE (
        pause->getToggleState());
    EXPECT_FALSE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Running");
    EXPECT_TRUE (
        timebase->isEnabled());
    EXPECT_FALSE (
        display->isPaused());
    EXPECT_FALSE (
        timescale
            ->getPausedState());

    options->setPausedState (
        true);
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Paused");
    options->setPausedState (
        false);
    EXPECT_EQ (
        handler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Running");

    pause->triggerClick();
    for (int attempt = 0;
         attempt < 50
             && ! pause
                       ->getToggleState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_TRUE (
        pause->getToggleState());
    EXPECT_TRUE (
        display->isPaused());
    EXPECT_TRUE (
        timescale
            ->getPausedState());

    pause->triggerClick();
    for (int attempt = 0;
         attempt < 50
             && pause
                    ->getToggleState();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    EXPECT_FALSE (
        pause->getToggleState());
    EXPECT_FALSE (
        display->isPaused());
    EXPECT_FALSE (
        timescale
            ->getPausedState());
}

TEST_F (LfpDisplayNodeTests,
        PauseToggleActionsIgnoreDisabledAndDestroyedControls)
{
    AccessibilityActions
        retainedActions;
    {
        auto canvas =
            std::make_unique<
                LfpViewer::
                    LfpDisplayCanvas> (
                processor,
                LfpViewer::
                    SplitLayouts::SINGLE,
                false);
        canvas->updateSettings();
        canvas->setSize (600, 800);
        canvas->addToDesktop (0);
        canvas->setVisible (true);

        const auto prefix =
            "oe.processor."
            + String (
                processor->getNodeId())
            + ".lfp.display_";
        auto* visiblePause =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + "1.pause"));
        ASSERT_NE (
            visiblePause,
            nullptr);
        ASSERT_TRUE (
            visiblePause->isShowing());

        auto* visibleHandler =
            visiblePause
                ->getAccessibilityHandler();
        ASSERT_NE (
            visibleHandler,
            nullptr);
        retainedActions =
            visibleHandler
                ->getActions();

        visiblePause->setEnabled (
            false);
        EXPECT_FALSE (
            visibleHandler
                ->isEnabled());
        bool disabledActionFound =
            false;
        std::thread disabledWorker (
            [&]
            {
                disabledActionFound =
                    retainedActions
                        .invoke (
                            AccessibilityActionType::
                                toggle);
            });
        disabledWorker.join();
        EXPECT_TRUE (
            disabledActionFound);
        EXPECT_FALSE (
            visiblePause
                ->getToggleState());
    }

    bool staleActionFound =
        false;
    std::thread staleWorker (
        [&]
        {
            staleActionFound =
                retainedActions
                    .invoke (
                        AccessibilityActionType::
                            toggle);
        });
    staleWorker.join();
    EXPECT_TRUE (
        staleActionFound);
}

TEST_F (LfpDisplayNodeTests,
        SingleLayoutSpaceKeyTogglesEveryDisplayPause)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    std::array<Button*, 3>
        pauseControls {};
    for (int displayIndex = 0;
         displayIndex < 3;
         ++displayIndex)
    {
        pauseControls[displayIndex] =
            dynamic_cast<Button*> (
                findLfpDescendantById (
                    *canvas,
                    prefix
                        + String (
                            displayIndex + 1)
                        + ".pause"));
        ASSERT_NE (
            pauseControls[displayIndex],
            nullptr);
        EXPECT_FALSE (
            pauseControls[displayIndex]
                ->getToggleState());
    }

    EXPECT_TRUE (
        canvas->keyPressed (
            KeyPress (
                KeyPress::spaceKey,
                ModifierKeys(),
                ' ')));
    for (auto* pause :
         pauseControls)
    {
        EXPECT_TRUE (
            pause
                ->getToggleState());
    }

    EXPECT_TRUE (
        canvas->keyPressed (
            KeyPress (
                KeyPress::spaceKey,
                ModifierKeys(),
                ' ')));
    for (auto* pause :
         pauseControls)
    {
        EXPECT_FALSE (
            pause
                ->getToggleState());
    }
}

TEST_F (LfpDisplayNodeTests,
        ProgrammaticDisplayParameterUpdatesRefreshAccessibleValues)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    auto* timebase =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "timebase"));
    auto* channelHeight =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_height"));
    auto* voltageRange =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "voltage_range"));
    ASSERT_NE (
        timebase,
        nullptr);
    ASSERT_NE (
        channelHeight,
        nullptr);
    ASSERT_NE (
        voltageRange,
        nullptr);
    auto timebaseHandler =
        timebase
            ->createAccessibilityHandler();
    auto channelHeightHandler =
        channelHeight
            ->createAccessibilityHandler();
    auto voltageRangeHandler =
        voltageRange
            ->createAccessibilityHandler();
    ASSERT_NE (
        timebaseHandler,
        nullptr);
    ASSERT_NE (
        channelHeightHandler,
        nullptr);
    ASSERT_NE (
        voltageRangeHandler,
        nullptr);

    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *timebase);
    ASSERT_NE (
        options,
        nullptr);

    options
        ->setTimebaseAndSelectionText (
            0.75f);
    options
        ->setSpreadSelection (
            55);
    options
        ->setRangeSelection (
            333.0f);

    EXPECT_EQ (
        timebaseHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "0.8");
    EXPECT_EQ (
        channelHeightHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "55");
    EXPECT_EQ (
        voltageRangeHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "333");

    const auto stableRangeId =
        voltageRange
            ->getComponentID();
    options->setSelectedType (
        ContinuousChannel::Type::
            AUX);
    EXPECT_EQ (
        voltageRangeHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Auto");
    EXPECT_EQ (
        voltageRangeHandler
            ->getDescription(),
        "Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");
    EXPECT_EQ (
        voltageRangeHandler
            ->getHelp(),
        "Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");
    options->setSelectedType (
        ContinuousChannel::Type::
            ADC);
    EXPECT_EQ (
        voltageRangeHandler
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "10.0");
    EXPECT_EQ (
        voltageRangeHandler
            ->getDescription(),
        "Choose the ADC voltage range shown in LFP display 1, in V.");
    EXPECT_EQ (
        voltageRangeHandler
            ->getHelp(),
        "Choose the ADC voltage range shown in LFP display 1, in V.");
    EXPECT_EQ (
        voltageRange
            ->getComponentID(),
        stableRangeId);
}

#if JUCE_WINDOWS
TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerExpandsAndCollapsesStreamSelector)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.stream";
    auto* selector =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (selector, nullptr);
    const auto expectedValue =
        processor
            ->getDisplayBuffers()[0]
            ->name;
    const std::wstring wideId (
        id.toWideCharPointer());
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto invokeUiaAction =
        [&] (
            LfpWindowsUiaAction action,
            bool expectedPopupActive)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        wideId,
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || selector
                                ->isPopupActive()
                            != expectedPopupActive);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();
        return actionResult;
    };

    const auto expandResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                expand,
            true);

    EXPECT_EQ (
        expandResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        expandResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        expandResult.enabled,
        TRUE);
    EXPECT_EQ (
        expandResult.name,
        L"LFP display 1 stream");
    EXPECT_EQ (
        expandResult.help,
        L"Choose the data stream shown in LFP display 1. This changes display routing only; acquisition and recording are unaffected.");
    EXPECT_EQ (
        expandResult
            .expandCollapsePatternResult,
        S_OK);
    EXPECT_TRUE (
        expandResult
            .expandCollapsePatternAvailable);
    EXPECT_EQ (
        expandResult.valuePatternResult,
        S_OK);
    EXPECT_TRUE (
        expandResult
            .valuePatternAvailable);
    EXPECT_EQ (
        expandResult.value,
        std::wstring (
            expectedValue
                .toWideCharPointer()));
    EXPECT_EQ (
        expandResult.expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (
        selector->isPopupActive());

    const auto repeatedExpandResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                expand,
            true);
    EXPECT_EQ (
        repeatedExpandResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        repeatedExpandResult
            .expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (
        selector->isPopupActive());

    const auto collapseResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                collapse,
            false);
    EXPECT_EQ (
        collapseResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        collapseResult.expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_FALSE (
        selector->isPopupActive());

    const auto repeatedCollapseResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                collapse,
            false);
    EXPECT_EQ (
        repeatedCollapseResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        repeatedCollapseResult
            .expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_FALSE (
        selector->isPopupActive());

    selector->setEnabled (false);
    const auto disabledExpandResult =
        invokeUiaAction (
            LfpWindowsUiaAction::
                expand,
            false);
    EXPECT_EQ (
        disabledExpandResult
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_FALSE (
        selector->isPopupActive());
    selector->setEnabled (true);

    auto* handler =
        selector
            ->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_TRUE (
        handler->getCurrentState()
            .isCollapsed());
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWritesAndSelectsStableStreamChoicesAcrossReorder)
{
    auto dynamicTester =
        std::make_unique<
            ProcessorTester> (
            TestSourceNodeBuilder (
                FakeSourceNodeParams {
                    4,
                    sampleRate,
                    bitVolts,
                    2 }),
            TestGuiRuntimeLifetime::
                process);
    auto* dynamicProcessor =
        dynamicTester
            ->createProcessor<
                LfpViewer::
                    LfpDisplayNode> (
                Plugin::Processor::SINK);
    auto* source =
        dynamic_cast<
            FakeSourceNode*> (
            dynamicTester
                ->getSourceNode());
    ASSERT_NE (source, nullptr);
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            dynamicProcessor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (
        600,
        800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());
    auto* splitter =
        getDisplaySplitters (
            *canvas)[0];
    auto* selector =
        splitter
            ->streamSelection
            .get();
    const auto parentId =
        selector
            ->getComponentID();
    const auto secondChoiceId =
        parentId
        + ".choice.2_fakesourcenode1_327c46616b65536f757263654e6f646531";
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (window, nullptr);
    const auto invokeFromWorker =
        [&] (
            const String& id,
            LfpWindowsUiaAction action,
            StringRef value = {})
    {
        LfpWindowsUiaInvokeResult
            result;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                result =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            id
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (value)
                                .toWideCharPointer()));
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
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return result;
    };

    const auto queryValue =
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        queryValue.invokeResult,
        S_OK);
    EXPECT_EQ (
        queryValue.valueReadOnly,
        FALSE);
    const auto setSecond =
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                setValue,
            "FakeSourceNode1");
    EXPECT_EQ (
        setSecond.invokeResult,
        S_OK);
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        "2|FakeSourceNode1");
    EXPECT_EQ (
        setSecond.value,
        L"FakeSourceNode1");
    EXPECT_EQ (
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                setValue,
            "FakeSourceNode0")
            .invokeResult,
        S_OK);
    const auto expandResult =
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        expandResult.focusResult,
        S_OK);
    EXPECT_EQ (
        expandResult.invokeResult,
        S_OK);
    const auto selectSecond =
        invokeFromWorker (
            secondChoiceId,
            LfpWindowsUiaAction::
                select);
    EXPECT_EQ (
        selectSecond.invokeResult,
        S_OK);
    EXPECT_EQ (
        selectSecond
            .selectionItemPatternAvailable,
        true);
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        "2|FakeSourceNode1");
    EXPECT_EQ (
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                collapse)
            .invokeResult,
        S_OK);

    source->moveStream (
        1,
        0);
    dynamicTester
        ->updateSourceNodeSettings();
    canvas->updateSettings();
    EXPECT_EQ (
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                setValue,
            "FakeSourceNode0")
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                expand)
            .invokeResult,
        S_OK);
    const auto selectSameIdAfterReorder =
        invokeFromWorker (
            secondChoiceId,
            LfpWindowsUiaAction::
                select);
    EXPECT_EQ (
        selectSameIdAfterReorder
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        splitter
            ->selectedStreamKey,
        "2|FakeSourceNode1");
    invokeFromWorker (
        parentId,
        LfpWindowsUiaAction::
            collapse);

    selector->setEnabled (
        false);
    EXPECT_EQ (
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                setValue,
            "FakeSourceNode0")
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    selector->setEnabled (
        true);
    canvas->setVisible (
        false);
    EXPECT_NE (
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                setValue,
            "FakeSourceNode0")
            .invokeResult,
        S_OK);
    canvas->setVisible (
        true);
    EXPECT_EQ (
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                setValue,
            "FakeSourceNode0")
            .invokeResult,
        S_OK);
    canvas.reset();
    EXPECT_NE (
        invokeFromWorker (
            parentId,
            LfpWindowsUiaAction::
                setValue,
            "FakeSourceNode0")
            .invokeResult,
        S_OK);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerWritesCoreDisplayParameters)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (600, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.timebase";
    auto* timebase =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        timebase,
        nullptr);
    const auto channelHeightId =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.channel_height";
    const auto voltageRangeId =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.voltage_range";
    auto* channelHeight =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                channelHeightId));
    auto* voltageRange =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                voltageRangeId));
    ASSERT_NE (
        channelHeight,
        nullptr);
    ASSERT_NE (
        voltageRange,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto writeValue =
        [&] (
            StringRef targetId,
            const std::wstring& value)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        LfpWindowsUiaAction::
                            setValue,
                        value);
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
        return actionResult;
    };

    const auto result =
        writeValue (
            id,
            L"0.75");
    EXPECT_EQ (
        result.invokeResult,
        S_OK);
    EXPECT_EQ (
        result.valuePatternResult,
        S_OK);
    EXPECT_TRUE (
        result
            .valuePatternAvailable);
    EXPECT_EQ (
        result.value,
        L"0.8");
    EXPECT_EQ (
        timebase->getText(),
        "0.8");

    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *timebase);
    ASSERT_NE (
        options,
        nullptr);
    options->setPausedState (
        true);
    const auto disabledResult =
        writeValue (
            id,
            L"1.0");
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_EQ (
        timebase->getText(),
        "0.8");
    options->setPausedState (
        false);

    const auto clampedHeightResult =
        writeValue (
            channelHeightId,
            L"2");
    EXPECT_EQ (
        clampedHeightResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        clampedHeightResult.value,
        L"6");
    EXPECT_EQ (
        channelHeight->getText(),
        "6");

    const auto clampedRangeResult =
        writeValue (
            voltageRangeId,
            L"10");
    EXPECT_EQ (
        clampedRangeResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        clampedRangeResult.value,
        L"25");
    EXPECT_EQ (
        voltageRange->getText(),
        "25");

    const auto customRangeResult =
        writeValue (
            voltageRangeId,
            L"333");
    EXPECT_EQ (
        customRangeResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        customRangeResult.value,
        L"333");
    EXPECT_EQ (
        voltageRange->getText(),
        "333");

    options->setSelectedType (
        ContinuousChannel::Type::
            AUX);
    const auto auxAutoResult =
        writeValue (
            voltageRangeId,
            L"Auto");
    EXPECT_EQ (
        auxAutoResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        auxAutoResult.value,
        L"Auto");
    EXPECT_EQ (
        auxAutoResult.help,
        L"Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");
    EXPECT_EQ (
        voltageRange->getText(),
        "Auto");

    const auto hiddenPaneResult =
        writeValue (
            "oe.processor."
                + String (
                    processor
                        ->getNodeId())
                + ".lfp.display_2.timebase",
            L"1.0");
    EXPECT_EQ (
        hiddenPaneResult
            .invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerSetsColourGroupingByShank)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    const auto id =
        prefix
        + "colour_grouping";
    auto* grouping =
        dynamic_cast<
            MessageThreadComboBox*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        grouping,
        nullptr);
    ASSERT_NE (
        display,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action,
            const std::wstring&
                valueToSet = {})
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action,
                        valueToSet);
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
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    MessageManager::getInstance()
        ->runDispatchLoopUntil (
            30);
    LfpThreadTrackingComboBoxListener
        listener;
    grouping->addListener (
        &listener);
    const auto selectedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            L"By Shank");
    grouping->removeListener (
        &listener);

    EXPECT_EQ (
        selectedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        selectedResult.controlType,
        UIA_ComboBoxControlTypeId);
    EXPECT_EQ (
        selectedResult.enabled,
        TRUE);
    EXPECT_EQ (
        selectedResult.name,
        L"LFP display 1 colour grouping");
    EXPECT_EQ (
        selectedResult.help,
        L"Choose how LFP display 1 assigns display colours: 1, 2, 4, 8, or 16 groups that many adjacent channels, while By Shank uses channel group metadata with a legacy depth-based fallback. This changes display colours only; acquisition and recording are unaffected.");
    EXPECT_EQ (
        selectedResult
            .valuePatternResult,
        S_OK);
    EXPECT_TRUE (
        selectedResult
            .valuePatternAvailable);
    EXPECT_EQ (
        selectedResult
            .valueReadOnly,
        FALSE);
    EXPECT_EQ (
        selectedResult.value,
        L"By Shank");
    EXPECT_EQ (
        selectedResult
            .expandCollapsePatternResult,
        S_OK);
    EXPECT_TRUE (
        selectedResult
            .expandCollapsePatternAvailable);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");

    const auto invalidValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            L"unsupported");
    EXPECT_EQ (
        invalidValueResult
            .invokeResult,
        E_INVALIDARG);
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");

    const auto expandedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        expandedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        expandedResult
            .expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (
        grouping->isPopupActive());
    const auto collapsedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                collapse);
    EXPECT_EQ (
        collapsedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        collapsedResult
            .expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_FALSE (
        grouping->isPopupActive());

    grouping->setEnabled (
        false);
    const auto disabledValueResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            L"8");
    EXPECT_EQ (
        disabledValueResult
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    const auto disabledExpandResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand);
    EXPECT_EQ (
        disabledExpandResult
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_EQ (
        grouping->getSelectedId(),
        6);
    EXPECT_EQ (
        display
            ->getColourGrouping(),
        "By Shank");

    const auto hiddenResult =
        runAction (
            prefix
                .replace (
                    "display_1.",
                    "display_2.")
                + "colour_grouping",
            LfpWindowsUiaAction::
                setValue,
            L"8");
    EXPECT_EQ (
        hiddenResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerSelectsChannelSkip)
{
    auto canvas =
        std::make_unique<LfpViewer::LfpDisplayCanvas> (
            processor,
            LfpViewer::SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (processor->getNodeId())
        + ".lfp.display_";
    const auto id = prefix + "1.channel_skip";
    auto* channelSkip =
        dynamic_cast<MessageThreadComboBox*> (
            findLfpDescendantById (*canvas, id));
    auto* display =
        findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (channelSkip, nullptr);
    ASSERT_NE (display, nullptr);
    const auto window =
        static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto runAction =
        [&] (StringRef targetId,
             LfpWindowsUiaAction action,
             StringRef value = {})
    {
        LfpWindowsUiaInvokeResult actionResult;
        std::atomic<bool> workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (targetId)
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (value)
                                .toWideCharPointer()));
                workerReturned.store (true);
            });
        for (int attempt = 0;
             attempt < 100 && ! workerReturned.load();
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (10);
        }
        joinLfpWorkerOrAbort (worker, workerReturned);
        return actionResult;
    };

    const auto description =
        L"Choose the visible-channel stride for LFP display 1: None shows every eligible channel; 2, 4, 8, 16, 32, or 64 shows every Nth eligible channel. This changes display density only; acquisition and recording are unaffected.";
    const auto setValueResult =
        runAction (id, LfpWindowsUiaAction::setValue, "32");
    EXPECT_EQ (setValueResult.invokeResult, S_OK);
    EXPECT_EQ (setValueResult.controlType, UIA_ComboBoxControlTypeId);
    EXPECT_EQ (setValueResult.enabled, TRUE);
    EXPECT_EQ (setValueResult.name, L"LFP display 1 channel skip");
    EXPECT_EQ (setValueResult.help, description);
    EXPECT_TRUE (setValueResult.valuePatternAvailable);
    EXPECT_EQ (setValueResult.valueReadOnly, FALSE);
    EXPECT_TRUE (setValueResult.expandCollapsePatternAvailable);
    EXPECT_TRUE (setValueResult.invokePatternAvailable);
    EXPECT_FALSE (setValueResult.selectionItemPatternAvailable);
    EXPECT_EQ (setValueResult.value, L"32");
    EXPECT_EQ (channelSkip->getSelectedId(), 6);
    EXPECT_EQ (display->getChannelDisplaySkipAmount(), 32);

    const auto expandResult =
        runAction (id, LfpWindowsUiaAction::expand);
    EXPECT_EQ (expandResult.invokeResult, S_OK);
    EXPECT_EQ (
        expandResult.expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_TRUE (channelSkip->isPopupActive());

    const auto choiceResult =
        runAction (
            id + ".choice.64",
            LfpWindowsUiaAction::select);
    EXPECT_EQ (choiceResult.invokeResult, S_OK);
    EXPECT_EQ (choiceResult.controlType, UIA_MenuItemControlTypeId);
    EXPECT_TRUE (choiceResult.selectionItemPatternAvailable);
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    const auto reexpandedResult =
        runAction (id, LfpWindowsUiaAction::expand);
    EXPECT_EQ (reexpandedResult.invokeResult, S_OK);
    const auto selectedChoiceResult =
        runAction (
            id + ".choice.64",
            LfpWindowsUiaAction::querySelection);
    EXPECT_EQ (selectedChoiceResult.invokeResult, S_OK);
    EXPECT_TRUE (selectedChoiceResult.selected);
    EXPECT_EQ (channelSkip->getSelectedId(), 7);
    EXPECT_EQ (display->getChannelDisplaySkipAmount(), 64);

    channelSkip->setEnabled (false);
    const auto disabledResult =
        runAction (id, LfpWindowsUiaAction::setValue, "8");
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (UIA_E_ELEMENTNOTENABLED));
    EXPECT_EQ (channelSkip->getSelectedId(), 7);
    channelSkip->setEnabled (true);

    const auto hiddenPaneResult =
        runAction (
            prefix + "2.channel_skip",
            LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (hiddenPaneResult.invokeResult, E_FAIL);
    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult =
        runAction (id, LfpWindowsUiaAction::queryValue);
    EXPECT_EQ (closedDrawerResult.invokeResult, E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesReverseOrder)
{
    auto canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (
        processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (true);
    ASSERT_TRUE (canvas->isShowing());

    const auto prefix = "oe.processor."
                        + String (processor->getNodeId())
                        + ".lfp.display_";
    const auto id = prefix + "1.reverse_order";
    auto* button = dynamic_cast<Button*> (
        findLfpDescendantById (*canvas, id));
    auto* display = findLfpDescendant<LfpViewer::LfpDisplay> (*canvas);
    ASSERT_NE (button, nullptr);
    ASSERT_NE (display, nullptr);
    const auto window = static_cast<HWND> (canvas->getWindowHandle());
    ASSERT_NE (window, nullptr);

    const auto runAction = [&] (StringRef targetId, LfpWindowsUiaAction action)
    {
        LfpWindowsUiaInvokeResult actionResult;
        std::atomic<bool> workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult = invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (String (targetId).toWideCharPointer()),
                    action);
                workerReturned.store (true);
            });
        for (int attempt = 0; attempt < 100 && ! workerReturned.load(); ++attempt)
            MessageManager::getInstance()->runDispatchLoopUntil (10);
        joinLfpWorkerOrAbort (worker, workerReturned);
        return actionResult;
    };

    const auto description =
        L"Reverse the current visible channel order in LFP display 1 after filtering, channel skipping, and optional metadata-based depth sorting. This changes display order only; acquisition and recording are unaffected.";
    const auto initialResult = runAction (id, LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (initialResult.invokeResult, S_OK);
    EXPECT_EQ (initialResult.controlType, UIA_CheckBoxControlTypeId);
    EXPECT_EQ (initialResult.enabled, TRUE);
    EXPECT_EQ (initialResult.name, L"LFP display 1 reverse channel order");
    EXPECT_EQ (initialResult.help, description);
    EXPECT_TRUE (initialResult.togglePatternAvailable);
    EXPECT_EQ (initialResult.toggleState, ToggleState_Off);
    EXPECT_TRUE (initialResult.valuePatternAvailable);
    EXPECT_EQ (initialResult.valueReadOnly, TRUE);
    EXPECT_EQ (initialResult.value, L"Off");
    EXPECT_FALSE (initialResult.invokePatternAvailable);

    const auto enabledResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (enabledResult.invokeResult, S_OK);
    EXPECT_EQ (enabledResult.toggleState, ToggleState_On);
    EXPECT_EQ (enabledResult.value, L"On");
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (display->getChannelsReversed());

    button->setEnabled (false);
    const auto disabledResult = runAction (id, LfpWindowsUiaAction::toggle);
    EXPECT_EQ (disabledResult.invokeResult,
               static_cast<HRESULT> (UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (button->getToggleState());
    EXPECT_TRUE (display->getChannelsReversed());
    const auto hiddenPaneResult = runAction (
        prefix + "2.reverse_order", LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (hiddenPaneResult.invokeResult, E_FAIL);

    button->setEnabled (true);
    canvas->toggleOptionsDrawer (false);
    const auto closedDrawerResult = runAction (
        id, LfpWindowsUiaAction::queryToggle);
    EXPECT_EQ (closedDrawerResult.invokeResult, E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesSortByDepth)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    canvas->toggleOptionsDrawer (
        true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.sort_by_depth";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* display =
        findLfpDescendant<
            LfpViewer::
                LfpDisplay> (
            *canvas);
    ASSERT_NE (
        button,
        nullptr);
    ASSERT_NE (
        display,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action);
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
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    const auto description =
        L"Turn metadata-based channel ordering on or off in LFP display 1. When enabled, visible channels are ordered using available group, depth, and horizontal-position metadata. This changes display order only; acquisition and recording are unaffected.";
    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryToggle);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_CheckBoxControlTypeId);
    EXPECT_EQ (
        initialResult.enabled,
        TRUE);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 sort channels by depth");
    EXPECT_EQ (
        initialResult.help,
        description);
    EXPECT_TRUE (
        initialResult
            .togglePatternAvailable);
    EXPECT_EQ (
        initialResult.toggleState,
        ToggleState_Off);
    EXPECT_TRUE (
        initialResult
            .valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        TRUE);
    EXPECT_EQ (
        initialResult.value,
        L"Off");
    EXPECT_FALSE (
        initialResult
            .invokePatternAvailable);

    const auto enabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle);
    EXPECT_EQ (
        enabledResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        enabledResult.toggleState,
        ToggleState_On);
    EXPECT_EQ (
        enabledResult.value,
        L"On");
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        display
            ->shouldOrderChannelsByDepth());

    button->setEnabled (
        false);
    const auto disabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle);
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (
        button->getToggleState());
    EXPECT_TRUE (
        display
            ->shouldOrderChannelsByDepth());

    const auto hiddenPaneResult =
        runAction (
            prefix
                + "2.sort_by_depth",
            LfpWindowsUiaAction::
                queryToggle);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);

    button->setEnabled (
        true);
    canvas->toggleOptionsDrawer (
        false);
    const auto closedDrawerResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryToggle);
    EXPECT_EQ (
        closedDrawerResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerReadsLatestNonzeroTtlWord)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.ttl_word";
    auto* label =
        dynamic_cast<Label*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        label,
        nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *label);
    ASSERT_NE (
        options,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action,
            StringRef value = {})
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action,
                        std::wstring (
                            String (value)
                                .toWideCharPointer()));
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
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_TextControlTypeId);
    EXPECT_EQ (
        initialResult.enabled,
        TRUE);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 latest non-zero TTL word");
    EXPECT_EQ (
        initialResult.help,
        L"Latest non-zero 64-bit TTL word received while its stream was selected in LFP display 1, shown as an unsigned decimal value. NONE means no non-zero word has been received by this display pane.");
    EXPECT_TRUE (
        initialResult
            .valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        TRUE);
    EXPECT_EQ (
        initialResult.value,
        L"NONE");
    EXPECT_FALSE (
        initialResult
            .invokePatternAvailable);
    EXPECT_FALSE (
        initialResult
            .togglePatternAvailable);

    options->setTTLWord (
        4294967297ULL);
    options->timerCallback();
    const auto updatedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        updatedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        updatedResult.name,
        initialResult.name);
    EXPECT_EQ (
        updatedResult.value,
        L"4294967297");

    const auto writeResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                setValue,
            "7");
    EXPECT_EQ (
        writeResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_INVALIDOPERATION));
    EXPECT_EQ (
        label->getText(),
        "4294967297");

    const auto hiddenPaneResult =
        runAction (
            prefix
                + "2.ttl_word",
            LfpWindowsUiaAction::
                queryValue);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesEventOverlayLine)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.event_overlay.line_1";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        button,
        nullptr);
    auto* eventInterface =
        findLfpAncestor<
            LfpViewer::
                EventDisplayInterface> (
            *button);
    ASSERT_NE (
        eventInterface,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action,
            bool expectedShown)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || eventInterface
                                ->getEventDisplayState()
                            != expectedShown);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryToggle,
            true);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_CheckBoxControlTypeId);
    EXPECT_EQ (
        initialResult.enabled,
        TRUE);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 event overlay line 1");
    EXPECT_EQ (
        initialResult.help,
        L"Show or hide TTL line 1 event markers in LFP display 1. This changes the display overlay only; acquisition and recording are unaffected.");
    EXPECT_TRUE (
        initialResult
            .togglePatternAvailable);
    EXPECT_FALSE (
        initialResult
            .invokePatternAvailable);
    EXPECT_TRUE (
        initialResult
            .valuePatternAvailable);
    EXPECT_EQ (
        initialResult.valueReadOnly,
        TRUE);
    EXPECT_EQ (
        initialResult.toggleState,
        ToggleState_On);
    EXPECT_EQ (
        initialResult.value,
        L"Shown");

    const auto hiddenResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        hiddenResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        hiddenResult.toggleState,
        ToggleState_Off);
    EXPECT_EQ (
        hiddenResult.value,
        L"Hidden");

    button->setEnabled (
        false);
    const auto disabledResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_FALSE (
        eventInterface
            ->getEventDisplayState());

    const auto hiddenPaneResult =
        runAction (
            prefix
                + "2.event_overlay.line_1",
            LfpWindowsUiaAction::
                queryToggle,
            false);
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerControlsOptionsDrawer)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_";
    const auto id =
        prefix
        + "1.options_drawer";
    auto* button =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (
        button,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef targetId,
            LfpWindowsUiaAction action,
            bool expectedOpen)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (
                                targetId)
                                .toWideCharPointer()),
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || canvas
                                ->optionsDrawerIsOpen
                            != expectedOpen);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        return actionResult;
    };

    const auto initialResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                queryExpansion,
            false);
    EXPECT_EQ (
        initialResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        initialResult.controlType,
        UIA_ButtonControlTypeId);
    EXPECT_EQ (
        initialResult.enabled,
        TRUE);
    EXPECT_EQ (
        initialResult.name,
        L"LFP display 1 options drawer");
    EXPECT_EQ (
        initialResult.help,
        L"Show or hide advanced options for all LFP displays. Opening the drawer reveals threshold, channel, signal-processing, and triggered-display controls.");
    EXPECT_TRUE (
        initialResult
            .expandCollapsePatternAvailable);
    EXPECT_FALSE (
        initialResult
            .togglePatternAvailable);
    EXPECT_TRUE (
        initialResult
            .invokePatternAvailable);
    EXPECT_EQ (
        initialResult
            .expansionState,
        ExpandCollapseState_Collapsed);

    LfpThreadTrackingButtonListener
        listener;
    button->addListener (
        &listener);
    const auto expandedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand,
            true);
    EXPECT_EQ (
        expandedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        expandedResult
            .expansionState,
        ExpandCollapseState_Expanded);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());

    const auto repeatedExpandResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                expand,
            true);
    EXPECT_EQ (
        repeatedExpandResult
            .invokeResult,
        S_OK);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);

    const auto collapsedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                collapse,
            false);
    EXPECT_EQ (
        collapsedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        collapsedResult
            .expansionState,
        ExpandCollapseState_Collapsed);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);

    const auto toggledResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        toggledResult.invokeResult,
        E_NOINTERFACE);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        2);

    const auto invokedResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                invoke,
            true);
    EXPECT_EQ (
        invokedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        3);
    const auto repeatedInvokeResult =
        runAction (
            id,
            LfpWindowsUiaAction::
                invoke,
            false);
    EXPECT_EQ (
        repeatedInvokeResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        4);
    button->removeListener (
        &listener);

    button->setEnabled (
        false);
    for (const auto action :
         { LfpWindowsUiaAction::
               expand,
           LfpWindowsUiaAction::
               invoke })
    {
        const auto disabledResult =
            runAction (
                id,
                action,
                false);
        EXPECT_EQ (
            disabledResult
                .invokeResult,
            static_cast<HRESULT> (
                UIA_E_ELEMENTNOTENABLED));
        EXPECT_FALSE (
            canvas
                ->optionsDrawerIsOpen);
    }

    const auto hiddenResult =
        runAction (
            prefix
                + "2.options_drawer",
            LfpWindowsUiaAction::
                queryExpansion,
            false);
    EXPECT_EQ (
        hiddenResult.invokeResult,
        E_FAIL);

    button->setEnabled (
        true);
    LfpDestroyCanvasButtonListener
        destroyListener (
            canvas);
    button->addListener (
        &destroyListener);
    LfpWindowsUiaInvokeResult
        destroyedResult;
    std::atomic<bool>
        destroyWorkerReturned {
            false
        };
    std::thread destroyWorker (
        [&]
        {
            destroyedResult =
                invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (
                        id
                            .toWideCharPointer()),
                    LfpWindowsUiaAction::
                        invoke);
            destroyWorkerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! destroyWorkerReturned
                       .load()
                 || canvas != nullptr);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        destroyWorker,
        destroyWorkerReturned);
    EXPECT_EQ (
        canvas,
        nullptr);
    EXPECT_EQ (
        destroyedResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerSelectsRangeTypeRadioButton)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    const auto auxId =
        prefix
        + "channel_type.aux";
    auto* data =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.data"));
    auto* aux =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                auxId));
    auto* adc =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                prefix
                    + "channel_type.adc"));
    auto* range =
        findLfpDescendantById (
            *canvas,
            prefix
                + "voltage_range");
    ASSERT_NE (data, nullptr);
    ASSERT_NE (aux, nullptr);
    ASSERT_NE (adc, nullptr);
    ASSERT_NE (range, nullptr);
    auto* options =
        findLfpAncestor<
            LfpViewer::
                LfpDisplayOptions> (
            *aux);
    ASSERT_NE (
        options,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    const auto runAction =
        [&] (
            StringRef id,
            LfpWindowsUiaAction action,
            ContinuousChannel::Type
                expectedType)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        std::wstring (
                            String (id)
                                .toWideCharPointer()),
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || options
                                ->getSelectedType()
                            != expectedType);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        joinLfpWorkerOrAbort (
            worker,
            workerReturned);
        EXPECT_TRUE (
            workerReturned.load());
        return actionResult;
    };

    aux->setEnabled (
        false);
    const auto disabledResult =
        runAction (
            auxId,
            LfpWindowsUiaAction::
                select,
            ContinuousChannel::Type::
                ELECTRODE);
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (
        data->getToggleState());
    EXPECT_FALSE (
        aux->getToggleState());
    EXPECT_EQ (
        options
            ->getSelectedType(),
        ContinuousChannel::Type::
            ELECTRODE);
    const auto disabledInvokeResult =
        runAction (
            auxId,
            LfpWindowsUiaAction::
                invoke,
            ContinuousChannel::Type::
                ELECTRODE);
    EXPECT_EQ (
        disabledInvokeResult
            .invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_TRUE (
        data->getToggleState());
    EXPECT_FALSE (
        aux->getToggleState());
    aux->setEnabled (
        true);

    LfpThreadTrackingButtonListener
        listener;
    aux->addListener (
        &listener);
    const auto auxResult =
        runAction (
            auxId,
            LfpWindowsUiaAction::
                select,
            ContinuousChannel::Type::
                AUX);
    aux->removeListener (
        &listener);

    EXPECT_EQ (
        auxResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        auxResult.controlType,
        UIA_RadioButtonControlTypeId);
    EXPECT_EQ (
        auxResult.enabled,
        TRUE);
    EXPECT_EQ (
        auxResult.name,
        L"LFP display 1 channel type AUX");
    EXPECT_EQ (
        auxResult.help,
        L"Select AUX channels as the target for the voltage range control in LFP display 1.");
    EXPECT_EQ (
        auxResult
            .selectionItemPatternResult,
        S_OK);
    EXPECT_TRUE (
        auxResult
            .selectionItemPatternAvailable);
    EXPECT_EQ (
        auxResult.selected,
        TRUE);
    EXPECT_FALSE (
        auxResult
            .togglePatternAvailable);
    EXPECT_FALSE (
        auxResult
            .valuePatternAvailable);
    EXPECT_EQ (
        auxResult
            .invokePatternResult,
        S_OK);
    EXPECT_TRUE (
        auxResult
            .invokePatternAvailable);
    EXPECT_EQ (
        listener
            .callbackCount
            .load(),
        1);
    EXPECT_TRUE (
        listener
            .callbackUsedMessageThread
            .load());
    EXPECT_FALSE (
        data->getToggleState());
    EXPECT_TRUE (
        aux->getToggleState());
    EXPECT_FALSE (
        adc->getToggleState());
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getValueInterface()
            ->getCurrentValueAsString(),
        "Auto");
    EXPECT_EQ (
        range
            ->getAccessibilityHandler()
            ->getHelp(),
        "Choose the AUX voltage range shown in LFP display 1, in mV, or Auto.");

    const auto hiddenResult =
        runAction (
            prefix
                .replace (
                    "display_1.",
                    "display_2.")
                + "channel_type.aux",
            LfpWindowsUiaAction::
                querySelection,
            ContinuousChannel::Type::
                AUX);
    EXPECT_EQ (
        hiddenResult.invokeResult,
        E_FAIL);
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaInvokeReportsDestroyedRangeTypeControl)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto id =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.channel_type.aux";
    auto* aux =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    ASSERT_NE (aux, nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);

    LfpDestroyCanvasButtonListener
        listener (canvas);
    aux->addListener (
        &listener);

    LfpWindowsUiaInvokeResult
        actionResult;
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            actionResult =
                invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (
                        id
                            .toWideCharPointer()),
                    LfpWindowsUiaAction::
                        invoke);
            workerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || canvas != nullptr);
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    joinLfpWorkerOrAbort (
        worker,
        workerReturned);

    EXPECT_EQ (
        canvas,
        nullptr);
    EXPECT_EQ (
        actionResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
}

TEST_F (LfpDisplayNodeTests,
        WindowsUiaWorkerTogglesPause)
{
    auto canvas =
        std::make_unique<
            LfpViewer::
                LfpDisplayCanvas> (
            processor,
            LfpViewer::
                SplitLayouts::SINGLE,
            false);
    canvas->updateSettings();
    canvas->setSize (1200, 800);
    canvas->addToDesktop (0);
    canvas->setVisible (true);
    ASSERT_TRUE (
        canvas->isShowing());

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".lfp.display_1.";
    const auto id =
        prefix + "pause";
    auto* pause =
        dynamic_cast<Button*> (
            findLfpDescendantById (
                *canvas,
                id));
    auto* timebase =
        findLfpDescendantById (
            *canvas,
            prefix
                + "timebase");
    ASSERT_NE (
        pause,
        nullptr);
    ASSERT_NE (
        timebase,
        nullptr);
    const auto window =
        static_cast<HWND> (
            canvas
                ->getWindowHandle());
    ASSERT_NE (
        window,
        nullptr);
    const auto wideId =
        std::wstring (
            id.toWideCharPointer());

    const auto runAction =
        [&] (
            LfpWindowsUiaAction action,
            bool expectedPaused)
    {
        LfpWindowsUiaInvokeResult
            actionResult;
        std::atomic<bool>
            workerReturned { false };
        std::thread worker (
            [&]
            {
                actionResult =
                    invokeLfpWindowsUiaControl (
                        window,
                        wideId,
                        action);
                workerReturned.store (
                    true);
            });
        for (int attempt = 0;
             attempt < 100
                 && (! workerReturned.load()
                     || pause
                                ->getToggleState()
                            != expectedPaused);
             ++attempt)
        {
            MessageManager::getInstance()
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();
        EXPECT_TRUE (
            workerReturned.load());
        return actionResult;
    };

    const auto pausedResult =
        runAction (
            LfpWindowsUiaAction::
                toggle,
            true);
    EXPECT_EQ (
        pausedResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        pausedResult.controlType,
        UIA_CheckBoxControlTypeId);
    EXPECT_EQ (
        pausedResult.enabled,
        TRUE);
    EXPECT_EQ (
        pausedResult.name,
        L"LFP display 1 pause");
    EXPECT_EQ (
        pausedResult.help,
        L"Pause or resume live updates in LFP display 1. Acquisition and recording continue.");
    EXPECT_EQ (
        pausedResult
            .togglePatternResult,
        S_OK);
    EXPECT_TRUE (
        pausedResult
            .togglePatternAvailable);
    EXPECT_EQ (
        pausedResult
            .toggleState,
        ToggleState_On);
    EXPECT_EQ (
        pausedResult
            .valuePatternResult,
        S_OK);
    EXPECT_TRUE (
        pausedResult
            .valuePatternAvailable);
    EXPECT_EQ (
        pausedResult
            .valueReadOnly,
        TRUE);
    EXPECT_EQ (
        pausedResult.value,
        L"Paused");
    EXPECT_TRUE (
        pause->getToggleState());
    EXPECT_FALSE (
        timebase->isEnabled());

    const auto runningResult =
        runAction (
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        runningResult.invokeResult,
        S_OK);
    EXPECT_EQ (
        runningResult
            .toggleState,
        ToggleState_Off);
    EXPECT_EQ (
        runningResult.value,
        L"Running");
    EXPECT_FALSE (
        pause->getToggleState());
    EXPECT_TRUE (
        timebase->isEnabled());

    pause->setEnabled (
        false);
    const auto disabledResult =
        runAction (
            LfpWindowsUiaAction::
                toggle,
            false);
    EXPECT_EQ (
        disabledResult.invokeResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTENABLED));
    EXPECT_FALSE (
        pause->getToggleState());

    LfpWindowsUiaInvokeResult
        hiddenPaneResult;
    std::atomic<bool>
        hiddenWorkerReturned { false };
    std::thread hiddenWorker (
        [&]
        {
            hiddenPaneResult =
                invokeLfpWindowsUiaControl (
                    window,
                    std::wstring (
                        (prefix
                         .replace (
                             "display_1.",
                             "display_2.")
                         + "pause")
                            .toWideCharPointer()),
                    LfpWindowsUiaAction::
                        queryToggle);
            hiddenWorkerReturned.store (
                true);
        });
    for (int attempt = 0;
         attempt < 100
             && ! hiddenWorkerReturned
                       .load();
         ++attempt)
    {
        MessageManager::getInstance()
            ->runDispatchLoopUntil (
                10);
    }
    hiddenWorker.join();
    EXPECT_TRUE (
        hiddenWorkerReturned.load());
    EXPECT_EQ (
        hiddenPaneResult.invokeResult,
        E_FAIL);
}
#endif

TEST_F (LfpDisplayNodeTests, VisualIntegrityTest)
{
    const int canvasX = 600;
    const int canvasY = 800;
    const float errorThreshold = 0.05f; //5% error threshold

    //Initialize LFP Canvas
    std::unique_ptr<LfpViewer::LfpDisplayCanvas> canvas = std::make_unique<LfpViewer::LfpDisplayCanvas> (processor, LfpViewer::SplitLayouts::SINGLE, false);
    canvas->updateSettings();
    canvas->setSize (canvasX, canvasY);
    canvas->resized();
    canvas->setVisible (true);

    //Get LFP size parameters from canvas
    setExpectedImageParameters (canvas.get());

    //Create snapshot of canvas channel bitmap and expected image
    juce::Rectangle<int> canvasSnapshot (
        x,
        y,
        width,
        height);
    ExpectedImage expected (numChannels, sampleRate * 2); //2 seconds to match canvas timebase

    processor->startAcquisition ();
    canvas->beginAnimation();

    //Add 5 10Hz waves with +-125uV amplitude
    auto inputBuffer = createBufferSinusoidal (5, numChannels, 1000, 125);
    writeBlock (inputBuffer);
    expected.addToBuffer (inputBuffer);
    canvas->refreshState();
    Image canvasImage = canvas->createComponentSnapshot (canvasSnapshot);
    Image expectedImage = expected.getImage (width, height, channelColours, backgroundColour, midlineColour, 125);
    int missCount = getImageDifferencePixelCount (expectedImage, canvasImage);
    EXPECT_LE (float(missCount) / float(width * height), errorThreshold);

    //Add 5 10Hz waves with +-250uV amplitude
    inputBuffer = createBufferSinusoidal (5, numChannels, 1000, 250);
    writeBlock (inputBuffer);
    expected.addToBuffer (inputBuffer);
    canvas->refreshState();

    canvasImage = canvas->createComponentSnapshot (canvasSnapshot);
    expectedImage = expected.getImage (width, height, channelColours, backgroundColour, midlineColour, 125);
    missCount = getImageDifferencePixelCount (expectedImage, canvasImage);
    EXPECT_LE (float (missCount) / float (width * height), errorThreshold);

    //Add 10 40Hz waves with +-250uV amplitude
    inputBuffer = createBufferSinusoidal (10, numChannels, 1000, 250);
    writeBlock (inputBuffer);
    expected.addToBuffer (inputBuffer);
    canvas->refreshState();

    canvasImage = canvas->createComponentSnapshot (canvasSnapshot);
    expectedImage = expected.getImage (width, height, channelColours, backgroundColour, midlineColour, 125);
    missCount = getImageDifferencePixelCount (expectedImage, canvasImage);
    EXPECT_LE (float (missCount) / float (width * height), errorThreshold);

    //Resize canvas to have half the vertical height and twice the uV range
    canvas->setChannelHeight (0, 20);
    canvas->setChannelRange (0, 500, ContinuousChannel::Type::ELECTRODE);
    canvas->refreshState();
    setExpectedImageParameters (canvas.get());

    canvasSnapshot.setBounds (x, y, width, height);

    canvasImage = canvas->createComponentSnapshot (canvasSnapshot);
    expectedImage = expected.getImage (width, height, channelColours, backgroundColour, midlineColour, 125);
    missCount = getImageDifferencePixelCount (expectedImage, canvasImage);
    EXPECT_LE (float (missCount) / float (width * height), errorThreshold);

    processor->stopAcquisition();
}

TEST_F (LfpDisplayNodeTests, DataIntegrityTest)
{
    int numSamples = 100;
    processor->startAcquisition ();

    auto inputBuffer = createBuffer (1000.0, 20.0, numChannels, numSamples);
    writeBlock (inputBuffer);

    processor->stopAcquisition();
}
