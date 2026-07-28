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

#include "../ChannelMap.h"
#include "../ChannelMapEditor.h"
#include "../../../Source/UI/SemanticComponent.h"
#include <ModelApplication.h>
#include <ModelProcessors.h>
#include <ProcessorHeaders.h>
#include <TestFixtures.h>
#include <atomic>
#include <filesystem>
#include <thread>

#if JUCE_WINDOWS
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

namespace
{
Component* findDescendantBySemanticId (
    Component& parent,
    StringRef id)
{
    if (parent.getComponentID()
        == id)
        return &parent;

    for (auto* child :
         parent.getChildren())
    {
        if (auto* result =
                findDescendantBySemanticId (
                    *child,
                    id))
            return result;
    }

    return nullptr;
}

class ThreadTrackingButtonListener final
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

#if JUCE_WINDOWS
HRESULT invokeWindowsUiaToggle (
    HWND window,
    const std::wstring& automationId)
{
    const auto comResult =
        CoInitializeEx (
            nullptr,
            COINIT_MULTITHREADED);
    if (FAILED (comResult))
        return comResult;

    const auto finish =
        [] (HRESULT result)
    {
        CoUninitialize();
        return result;
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
    if (FAILED (result)
        || element == nullptr)
    {
        return finish (
            FAILED (result)
                ? result
                : E_FAIL);
    }

    Microsoft::WRL::ComPtr<
        IUIAutomationTogglePattern>
        togglePattern;
    result =
        element
            ->GetCurrentPatternAs (
                UIA_TogglePatternId,
                IID_PPV_ARGS (
                    &togglePattern));
    if (FAILED (result)
        || togglePattern == nullptr)
    {
        return finish (
            FAILED (result)
                ? result
                : E_NOINTERFACE);
    }

    return finish (
        togglePattern->Toggle());
}
#endif
} // namespace

class ChannelMapTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageManager::getInstance();
        numChannels = 8;
        tester = std::make_unique<ProcessorTester> (TestSourceNodeBuilder (FakeSourceNodeParams {
            numChannels,
            sampleRate,
            1.0,
        }));
        processor = tester->createProcessor<ChannelMap> (Plugin::Processor::FILTER);
        ASSERT_EQ (processor->getNumDataStreams(), 1);
        streamId = processor->getDataStreams()[0]->getStreamId();

        prbFilePath = std::filesystem::temp_directory_path() / "prb_file.json";
    }

    void TearDown() override
    {
        if (std::filesystem::exists (prbFilePath))
        {
            std::filesystem::remove (prbFilePath);
        }
    }

    ChannelMap* processor;
    int numChannels;
    uint16 streamId;
    std::unique_ptr<ProcessorTester> tester;
    float sampleRate = 30000.0;
    std::filesystem::path prbFilePath;

    GenericEditor* createPopulatedEditor()
    {
        processor->setHeadlessMode (false);
        processor->setProcessorType (
            Plugin::Processor::SPLITTER);
        auto* editor =
            static_cast<GenericEditor*> (
                static_cast<
                    GenericProcessor*> (
                    processor)
                    ->createEditor());
        if (editor != nullptr)
        {
            // Construct without the stream-selector table, which the
            // headless fixture does not own, then restore the real
            // processor kind before exercising signal-chain updates.
            processor->setProcessorType (
                Plugin::Processor::FILTER);
            editor->addToDesktop (0);
            editor->update (true);
        }
        return editor;
    }

    String getSlotSemanticId (
        int oneBasedSlot) const
    {
        auto* stream =
            processor->getDataStream (
                streamId);
        const auto streamIdentifier =
            stream->getIdentifier()
                    .isNotEmpty()
                ? stream
                      ->getIdentifier()
                : stream->getName();
        return "oe.processor."
               + String (
                   processor->getNodeId())
               + ".channel_map.source_"
               + String (
                   stream
                       ->getSourceNodeId())
               + ".stream_"
               + sanitiseSemanticSegment (
                   streamIdentifier)
               + ".slot_"
               + String (oneBasedSlot);
    }
};

TEST_F (ChannelMapTests,
        ExposesChannelMapFileControls)
{
    processor->setHeadlessMode (false);
    processor->setProcessorType (
        Plugin::Processor::SPLITTER);
    auto* editor =
        static_cast<
            GenericProcessor*> (
                processor)
            ->createEditor();
    ASSERT_NE (editor, nullptr);

    const auto prefix =
        "oe.processor."
        + String (
            processor->getNodeId())
        + ".channel_map";
    auto* load =
        findDescendantBySemanticId (
            *editor,
            prefix + ".load_prb");
    auto* save =
        findDescendantBySemanticId (
            *editor,
            prefix + ".save_prb");
    ASSERT_NE (load, nullptr);
    EXPECT_EQ (
        load->getTitle(),
        "Load channel map...");
    EXPECT_EQ (
        load->getDescription(),
        "Load channel map settings for the selected data stream from a .prb file.");
    ASSERT_NE (save, nullptr);
    EXPECT_EQ (
        save->getTitle(),
        "Save channel map...");
    EXPECT_EQ (
        save->getDescription(),
        "Save channel map settings for the selected data stream to a .prb file.");
}

TEST_F (ChannelMapTests,
        ChannelMapFileButtonActionsAreWorkerSafe)
{
    auto verifyButton =
        [] (auto button,
            StringRef semanticId,
            StringRef title)
    {
        applySemanticMetadata (
            *button,
            semanticId,
            title,
            "Choose a channel map file.",
            "Opens the system file chooser.");
        button
            ->refreshAccessibilityState();
        ThreadTrackingButtonListener
            listener;
        button->addListener (&listener);
        auto handler =
            button
                ->createAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::button);
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::press));
        EXPECT_FALSE (
            handler->getActions().contains (
                AccessibilityActionType::toggle));
        EXPECT_EQ (
            handler->getValueInterface(),
            nullptr);

        String workerTitle;
        String workerDescription;
        String workerHelp;
        bool workerEnabled = false;
        std::atomic<bool>
            actionReturned { false };
        std::thread worker (
            [&]
            {
                workerTitle =
                    handler->getTitle();
                workerDescription =
                    handler
                        ->getDescription();
                workerHelp =
                    handler->getHelp();
                workerEnabled =
                    handler->isEnabled();
                actionReturned.store (
                    handler
                        ->getActions()
                        .invoke (
                            AccessibilityActionType::
                                press));
            });

        auto* messageManager =
            MessageManager::getInstance();
        for (int attempt = 0;
             attempt < 20
                 && (! actionReturned.load()
                     || listener
                                .callbackCount
                                .load()
                            == 0);
             ++attempt)
        {
            messageManager
                ->runDispatchLoopUntil (
                    10);
        }
        worker.join();

        EXPECT_EQ (
            workerTitle,
            title);
        EXPECT_EQ (
            workerDescription,
            "Choose a channel map file.");
        EXPECT_EQ (
            workerHelp,
            "Opens the system file chooser.");
        EXPECT_TRUE (workerEnabled);
        EXPECT_TRUE (
            actionReturned.load());
        EXPECT_EQ (
            listener.callbackCount.load(),
            1);
        EXPECT_TRUE (
            listener
                .callbackUsedMessageThread
                .load());

        button->setEnabled (false);
        const auto actions =
            handler->getActions();
        EXPECT_TRUE (
            actions.invoke (
                AccessibilityActionType::
                    press));
        messageManager
            ->runDispatchLoopUntil (10);
        EXPECT_EQ (
            listener.callbackCount.load(),
            1);
        EXPECT_FALSE (
            handler->isEnabled());

        handler.reset();
        button.reset();
        EXPECT_TRUE (
            actions.invoke (
                AccessibilityActionType::
                    press));
        messageManager
            ->runDispatchLoopUntil (10);
        EXPECT_EQ (
            listener.callbackCount.load(),
            1);
    };

    verifyButton (
        std::make_unique<
            ChannelMapLoadButton> (
            "Load channel map"),
        "oe.test.channel_map.load_prb",
        "Load channel map...");
    verifyButton (
        std::make_unique<
            ChannelMapSaveButton> (
            "Save channel map"),
        "oe.test.channel_map.save_prb",
        "Save channel map...");
}

TEST_F (ChannelMapTests,
        ExposesStableChannelMappingSlots)
{
    auto* editor =
        createPopulatedEditor();
    ASSERT_NE (editor, nullptr);
    auto* stream =
        processor->getDataStream (
            streamId);
    ASSERT_NE (stream, nullptr);
    ASSERT_NE (
        stream->getIdentifier(),
        stream->getName());
    EXPECT_TRUE (
        getSlotSemanticId (1)
            .contains (
                ".stream_identifier.slot_1"));
    EXPECT_FALSE (
        getSlotSemanticId (1)
            .contains (
                ".stream_fakesourcenode0."));

    StringArray ids;
    for (int slot = 0;
         slot < numChannels;
         ++slot)
    {
        const auto id =
            getSlotSemanticId (
                slot + 1);
        ids.add (id);
        auto* component =
            findDescendantBySemanticId (
                *editor,
                id);
        ASSERT_NE (component, nullptr)
            << id;
        auto* handler =
            component
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getRole(),
            AccessibilityRole::toggleButton);
        EXPECT_TRUE (
            handler->getActions().contains (
                AccessibilityActionType::toggle));
        EXPECT_FALSE (
            handler->getActions().contains (
                AccessibilityActionType::press));
        ASSERT_NE (
            handler->getValueInterface(),
            nullptr);
        EXPECT_EQ (
            handler->getValueInterface()
                ->getCurrentValueAsString(),
            "Enabled");
        EXPECT_TRUE (
            handler->getCurrentState()
                .isCheckable());
        EXPECT_TRUE (
            handler->getCurrentState()
                .isChecked());
        EXPECT_TRUE (
            handler->isEnabled());
        EXPECT_EQ (
            handler->getTitle(),
            "Mapping position "
                + String (slot + 1)
                + ": input channel "
                + String (slot + 1));
        EXPECT_TRUE (
            handler->getDescription()
                .contains (
                    "Toggle to include or exclude this input channel"));
        EXPECT_TRUE (
            handler->getDescription()
                .contains (
                    "drag in the GUI to reorder mapping positions"));
        EXPECT_EQ (
            handler->getHelp(),
            handler->getDescription());
    }

    EXPECT_EQ (
        ids.size(),
        numChannels);
    ids.removeDuplicates (false);
    EXPECT_EQ (
        ids.size(),
        numChannels);

    Array<int> reversedOrder;
    for (int channel =
             numChannels - 1;
         channel >= 0;
         --channel)
    {
        reversedOrder.add (channel);
    }
    processor->setChannelOrder (
        streamId,
        reversedOrder);
    editor->updateSettings();

    for (int slot = 0;
         slot < numChannels;
         ++slot)
    {
        auto* component =
            findDescendantBySemanticId (
                *editor,
                getSlotSemanticId (
                    slot + 1));
        ASSERT_NE (component, nullptr);
        auto* handler =
            component
                ->getAccessibilityHandler();
        ASSERT_NE (handler, nullptr);
        EXPECT_EQ (
            handler->getTitle(),
            "Mapping position "
                + String (slot + 1)
                + ": input channel "
                + String (
                    numChannels - slot));
    }
}

TEST_F (ChannelMapTests,
        ChannelMappingSlotActionsAreSafeAndHonourLocks)
{
    auto* editor =
        createPopulatedEditor();
    ASSERT_NE (editor, nullptr);
    const auto slotId =
        getSlotSemanticId (1);

    auto findSlot =
        [&]() -> Component*
    {
        return findDescendantBySemanticId (
            *editor,
            slotId);
    };

    auto* slot = findSlot();
    ASSERT_NE (slot, nullptr);
    auto* handler =
        slot->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto enabledActions =
        handler->getActions();

    std::atomic<bool>
        workerReturned { false };
    bool workerInvoked = false;
    String workerTitle;
    String workerDescription;
    String workerHelp;
    String workerValue;
    bool workerEnabled = false;
    bool workerCheckable = false;
    bool workerChecked = false;
    std::thread worker (
        [&]
        {
            workerTitle =
                handler->getTitle();
            workerDescription =
                handler
                    ->getDescription();
            workerHelp =
                handler->getHelp();
            workerValue =
                handler
                    ->getValueInterface()
                    ->getCurrentValueAsString();
            workerEnabled =
                handler->isEnabled();
            const auto workerState =
                handler
                    ->getCurrentState();
            workerCheckable =
                workerState.isCheckable();
            workerChecked =
                workerState.isChecked();
            workerInvoked =
                enabledActions.invoke (
                    AccessibilityActionType::
                        toggle);
            workerReturned.store (true);
        });

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 50
             && (! workerReturned.load()
                 || processor
                        ->getChannelEnabledState (
                            streamId)[0]);
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    worker.join();

    EXPECT_TRUE (workerInvoked);
    ASSERT_TRUE (
        workerReturned.load());
    EXPECT_EQ (
        workerTitle,
        "Mapping position 1: input channel 1");
    EXPECT_TRUE (
        workerDescription.contains (
            "Toggle to include or exclude this input channel"));
    EXPECT_EQ (
        workerHelp,
        workerDescription);
    EXPECT_EQ (
        workerValue,
        "Enabled");
    EXPECT_TRUE (workerEnabled);
    EXPECT_TRUE (workerCheckable);
    EXPECT_TRUE (workerChecked);
    ASSERT_FALSE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);

    slot = findSlot();
    ASSERT_NE (slot, nullptr);
    handler =
        slot->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_TRUE (
        handler->isEnabled());
    EXPECT_TRUE (
        handler->getCurrentState()
            .isCheckable());
    EXPECT_FALSE (
        handler->getCurrentState()
            .isChecked());
    EXPECT_EQ (
        handler->getValueInterface()
            ->getCurrentValueAsString(),
        "Disabled");

    const auto reenableActions =
        handler->getActions();
    EXPECT_TRUE (
        reenableActions.invoke (
            AccessibilityActionType::
                toggle));
    for (int attempt = 0;
         attempt < 50
             && ! processor
                     ->getChannelEnabledState (
                         streamId)[0];
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    ASSERT_TRUE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);

    slot = findSlot();
    ASSERT_NE (slot, nullptr);
    handler =
        slot->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    const auto staleActions =
        handler->getActions();
    std::atomic<bool>
        staleWorkerStarted { false };
    std::atomic<bool>
        staleWorkerReturned { false };
    bool staleWorkerInvoked = false;
    std::thread staleWorker (
        [&]
        {
            staleWorkerStarted.store (
                true);
            staleWorkerInvoked =
                staleActions.invoke (
                    AccessibilityActionType::
                        toggle);
            staleWorkerReturned.store (
                true);
        });
    while (! staleWorkerStarted.load())
        std::this_thread::yield();
    editor->updateSettings();
    for (int attempt = 0;
         attempt < 50
             && ! staleWorkerReturned
                     .load();
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    staleWorker.join();
    messageManager
        ->runDispatchLoopUntil (20);
    EXPECT_TRUE (staleWorkerInvoked);
    ASSERT_TRUE (
        staleWorkerReturned.load());
    EXPECT_TRUE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);

    slot = findSlot();
    ASSERT_NE (slot, nullptr);
    handler =
        slot->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    editor->editorStartAcquisition();
    EXPECT_FALSE (
        handler->isEnabled());
    const auto lockedActions =
        handler->getActions();
    EXPECT_TRUE (
        lockedActions.invoke (
            AccessibilityActionType::
                toggle));
    editor->editorStopAcquisition();
    EXPECT_TRUE (
        handler->isEnabled());
    messageManager
        ->runDispatchLoopUntil (20);
    EXPECT_TRUE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);

    EXPECT_TRUE (
        handler->getActions().invoke (
            AccessibilityActionType::
                toggle));
    for (int attempt = 0;
         attempt < 50
             && processor
                    ->getChannelEnabledState (
                        streamId)[0];
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    EXPECT_FALSE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);

    auto* undoManager =
        CoreServices::getUndoManager();
    ASSERT_NE (undoManager, nullptr);
    EXPECT_TRUE (
        undoManager->undo());
    EXPECT_TRUE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);
    slot = findSlot();
    ASSERT_NE (slot, nullptr);
    handler =
        slot->getAccessibilityHandler();
    ASSERT_NE (handler, nullptr);
    EXPECT_TRUE (
        handler->getCurrentState()
            .isChecked());
}

#if JUCE_WINDOWS
TEST_F (ChannelMapTests,
        WindowsUiaToggleReturnsSuccessBeforeSlotRebuild)
{
    auto* editor =
        createPopulatedEditor();
    ASSERT_NE (editor, nullptr);
    editor->setBounds (
        0,
        0,
        335,
        130);
    editor->setVisible (true);
    editor->setAlwaysOnTop (true);
    editor->toFront (false);
    ASSERT_TRUE (
        editor->isShowing());

    const auto slotId =
        getSlotSemanticId (1);
    ASSERT_NE (
        findDescendantBySemanticId (
            *editor,
            slotId),
        nullptr);

    Microsoft::WRL::ComPtr<
        IUIAutomation>
        automation;
    ASSERT_TRUE (
        SUCCEEDED (
            CoCreateInstance (
                CLSID_CUIAutomation,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS (
                    &automation))));

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        rootElement;
    ASSERT_TRUE (
        SUCCEEDED (
            automation
                ->ElementFromHandle (
                    static_cast<HWND> (
                        editor
                            ->getWindowHandle()),
                    &rootElement)));

    VARIANT expectedId;
    VariantInit (&expectedId);
    expectedId.vt = VT_BSTR;
    expectedId.bstrVal =
        SysAllocString (
            slotId
                .toWideCharPointer());
    ASSERT_NE (
        expectedId.bstrVal,
        nullptr);
    Microsoft::WRL::ComPtr<
        IUIAutomationCondition>
        idCondition;
    const auto conditionResult =
        automation
            ->CreatePropertyCondition (
                UIA_AutomationIdPropertyId,
                expectedId,
                &idCondition);
    VariantClear (&expectedId);
    ASSERT_TRUE (
        SUCCEEDED (
            conditionResult));

    Microsoft::WRL::ComPtr<
        IUIAutomationElement>
        slotElement;
    ASSERT_TRUE (
        SUCCEEDED (
            rootElement
                ->FindFirst (
                    TreeScope_Subtree,
                    idCondition.Get(),
                    &slotElement)));
    ASSERT_NE (
        slotElement.Get(),
        nullptr);

    Microsoft::WRL::ComPtr<
        IUIAutomationTogglePattern>
        togglePattern;
    ASSERT_TRUE (
        SUCCEEDED (
            slotElement
                ->GetCurrentPatternAs (
                    UIA_TogglePatternId,
                    IID_PPV_ARGS (
                        &togglePattern))));
    ASSERT_NE (
        togglePattern.Get(),
        nullptr);

    const auto toggleResult =
        togglePattern->Toggle();
    EXPECT_EQ (
        toggleResult,
        S_OK);
    EXPECT_NE (
        toggleResult,
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
    EXPECT_TRUE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 50
             && processor
                    ->getChannelEnabledState (
                        streamId)[0];
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    EXPECT_FALSE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);
    EXPECT_NE (
        findDescendantBySemanticId (
            *editor,
            slotId),
        nullptr);

    togglePattern.Reset();
    slotElement.Reset();
    ASSERT_TRUE (
        SUCCEEDED (
            rootElement
                ->FindFirst (
                    TreeScope_Subtree,
                    idCondition.Get(),
                    &slotElement)));
    ASSERT_NE (
        slotElement.Get(),
        nullptr);
    BOOL uiaEnabled = FALSE;
    ASSERT_TRUE (
        SUCCEEDED (
            slotElement
                ->get_CurrentIsEnabled (
                    &uiaEnabled)));
    EXPECT_EQ (
        uiaEnabled,
        TRUE);
    ASSERT_TRUE (
        SUCCEEDED (
            slotElement
                ->GetCurrentPatternAs (
                    UIA_TogglePatternId,
                    IID_PPV_ARGS (
                        &togglePattern))));
    ASSERT_NE (
        togglePattern.Get(),
        nullptr);
    EXPECT_EQ (
        togglePattern->Toggle(),
        S_OK);
    for (int attempt = 0;
         attempt < 50
             && ! processor
                     ->getChannelEnabledState (
                         streamId)[0];
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    EXPECT_TRUE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);
}

TEST_F (ChannelMapTests,
        WindowsUiaWorkerToggleReturnsSuccessBeforeSlotRebuild)
{
    auto* editor =
        createPopulatedEditor();
    ASSERT_NE (editor, nullptr);
    editor->setBounds (
        0,
        0,
        335,
        130);
    editor->setVisible (true);
    editor->setAlwaysOnTop (true);
    editor->toFront (false);
    ASSERT_TRUE (
        editor->isShowing());

    const auto slotId =
        getSlotSemanticId (1);
    const std::wstring
        wideSlotId (
            slotId
                .toWideCharPointer());
    const auto window =
        static_cast<HWND> (
            editor
                ->getWindowHandle());
    ASSERT_NE (window, nullptr);

    std::atomic<HRESULT>
        toggleResult { E_PENDING };
    std::atomic<bool>
        workerReturned { false };
    std::thread worker (
        [&]
        {
            toggleResult.store (
                invokeWindowsUiaToggle (
                    window,
                    wideSlotId));
            workerReturned.store (
                true);
        });

    auto* messageManager =
        MessageManager::getInstance();
    for (int attempt = 0;
         attempt < 100
             && (! workerReturned.load()
                 || processor
                        ->getChannelEnabledState (
                            streamId)[0]);
         ++attempt)
    {
        messageManager
            ->runDispatchLoopUntil (10);
    }
    worker.join();

    EXPECT_EQ (
        toggleResult.load(),
        S_OK);
    EXPECT_NE (
        toggleResult.load(),
        static_cast<HRESULT> (
            UIA_E_ELEMENTNOTAVAILABLE));
    EXPECT_FALSE (
        processor
            ->getChannelEnabledState (
                streamId)[0]);
    EXPECT_NE (
        findDescendantBySemanticId (
            *editor,
            slotId),
        nullptr);
}
#endif

TEST_F (ChannelMapTests, TestRemapsChannels)
{
    // Make it backwards:
    juce::Array<int> newChanOrder;
    std::unordered_map<int, int> oldToNewChanMapping;
    for (int i = 0; i < numChannels; i++)
    {
        newChanOrder.add (numChannels - 1 - i);
        oldToNewChanMapping[i] = numChannels - 1 - i;
    }

    processor->setChannelOrder (streamId, newChanOrder);
    processor->updateSettings();

    auto originalDataStream = tester->getSourceNodeDataStream (streamId);
    auto mappedDataStream = tester->getProcessorDataStream (processor->getNodeId(), streamId);

    ASSERT_EQ (mappedDataStream->getContinuousChannels().size(),
               originalDataStream->getContinuousChannels().size());
    for (std::pair<int, int> oldToNew : oldToNewChanMapping)
    {
        ASSERT_EQ (
            mappedDataStream->getContinuousChannels()[oldToNew.second]->getGlobalIndex(),
            originalDataStream->getContinuousChannels()[oldToNew.first]->getGlobalIndex());
    }
}

TEST_F (ChannelMapTests, TestRespectsEnabledChannels)
{
    int expectedMappedNumChans = 4;
    // Make it backwards:
    juce::Array<int> newChanOrder;
    std::unordered_map<int, int> oldToNewChanMapping;
    for (int i = 0; i < numChannels; i++)
    {
        if (i < expectedMappedNumChans)
        {
            // Enable channels explicitly
            processor->setChannelEnabled (streamId, i, 1);
            oldToNewChanMapping[i] = i;
        }
        else
        {
            // Disable some channels
            processor->setChannelEnabled (streamId, i, 0);
        }

        // Always map all channels, or else it'll be ignored
        newChanOrder.add (i);
    }

    processor->setChannelOrder (streamId, newChanOrder);
    processor->updateSettings();

    auto originalDataStream = tester->getSourceNodeDataStream (streamId);
    auto mappedDataStream = tester->getProcessorDataStream (processor->getNodeId(), streamId);

    ASSERT_EQ (originalDataStream->getContinuousChannels().size(), numChannels);
    ASSERT_EQ (mappedDataStream->getContinuousChannels().size(), expectedMappedNumChans);
    for (std::pair<int, int> oldToNew : oldToNewChanMapping)
    {
        ASSERT_EQ (
            mappedDataStream->getContinuousChannels()[oldToNew.second]->getGlobalIndex(),
            originalDataStream->getContinuousChannels()[oldToNew.first]->getGlobalIndex());
    }
}

TEST_F (ChannelMapTests, ConfigFileTest)
{
    std::vector<std::string> prbFileContentsList = {
        "{",
        "  \"0\": {",
        "    \"mapping\": [",
        "      7,",
        "      6,",
        "      5,",
        "      4,",
        "      3,",
        "      2,",
        "      1,",
        "      0",
        "    ],",
        "    \"enabled\": [",
        "      true,",
        "      true,",
        "      true,",
        "      true,",
        "      true,",
        "      true,",
        "      true,",
        "      true",
        "    ]",
        "  }",
        "}"
    };

    std::stringstream prbFileSs;
    for (const auto& line : prbFileContentsList)
    {
        prbFileSs << line << std::endl;
    }

    std::string prbFileContents = prbFileSs.str();
    auto f = juce::File (prbFilePath.string());
    {
        // Write and close it via braces
        juce::FileOutputStream outStream (f);
        outStream.writeString (prbFileContents);
    }

    processor->loadStreamSettings (streamId, f);
    processor->updateSettings();

    auto originalDataStream = tester->getSourceNodeDataStream (streamId);
    auto mappedDataStream = tester->getProcessorDataStream (processor->getNodeId(), streamId);

    ASSERT_EQ (originalDataStream->getContinuousChannels().size(), numChannels);
    ASSERT_EQ (
        mappedDataStream->getContinuousChannels().size(),
        originalDataStream->getContinuousChannels().size());
    for (std::pair<int, int> oldToNew :
         std::unordered_map<int, int> ({
             { 0, 7 },
             { 1, 6 },
             { 2, 5 },
             { 3, 4 },
             { 4, 3 },
             { 5, 2 },
             { 6, 1 },
             { 7, 0 },
         }))
    {
        ASSERT_EQ (
            mappedDataStream->getContinuousChannels()[oldToNew.second]->getGlobalIndex(),
            originalDataStream->getContinuousChannels()[oldToNew.first]->getGlobalIndex());
    }
}
